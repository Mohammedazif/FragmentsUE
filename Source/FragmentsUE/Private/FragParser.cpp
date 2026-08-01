#include "FragParser.h"
#include "FragmentsUEModule.h"
#include "earcut.hpp"

// FlatBuffers
THIRD_PARTY_INCLUDES_START
#include "flatbuffers/flatbuffers.h"
#include "index_generated.h"
THIRD_PARTY_INCLUDES_END

#include "Misc/FileHelper.h"
#include "Misc/Compression.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

// ─────────────────────────────────────────────────────────────────────────────
// Public API
// ─────────────────────────────────────────────────────────────────────────────

FFragImportResult FFragParser::LoadFromFile(const FString& FilePath, const FFragImportOptions& Options)
{
	TArray<uint8> RawData;
	if (!FFileHelper::LoadFileToArray(RawData, *FilePath))
	{
		FFragImportResult Result;
		Result.bSuccess = false;
		Result.ErrorMessage = FString::Printf(TEXT("Failed to read file: %s"), *FilePath);
		UE_LOG(LogFragmentsUE, Error, TEXT("%s"), *Result.ErrorMessage);
		return Result;
	}

	FFragImportResult Result = LoadFromBuffer(RawData, Options);

	// Derive a clean model name from the file path (strip directory and extension)
	// e.g. "D:/Models/Joyson Model.frag" → "Joyson_Model"
	FString BaseName = FPaths::GetBaseFilename(FilePath);
	BaseName = BaseName.Replace(TEXT(" "), TEXT("_"));
	Result.ModelName = BaseName;
	Result.ModelInfo.Name = BaseName;

	return Result;
}


FFragImportResult FFragParser::LoadFromBuffer(const TArray<uint8>& RawData, const FFragImportOptions& Options)
{
	if (RawData.Num() == 0)
	{
		FFragImportResult Result;
		Result.bSuccess = false;
		Result.ErrorMessage = TEXT("Empty buffer");
		return Result;
	}

	// Try decompression (Fragments files may be zlib-compressed)
	TArray<uint8> Decompressed;
	const uint8* Buffer;
	int32 BufferSize;

	if (TryDecompress(RawData, Decompressed))
	{
		Buffer = Decompressed.GetData();
		BufferSize = Decompressed.Num();
	}
	else
	{
		Buffer = RawData.GetData();
		BufferSize = RawData.Num();
	}

	// Verify FlatBuffers integrity
	// NOTE: ThatOpen's JS library serializes with builder.finish() WITHOUT a file identifier,
	// so we must NOT check for the "0001" identifier. Use VerifyBuffer<Model>(nullptr) instead.
	// Attributes, relations and profiles are all vectors of tables, so the table count
	// scales with the model — the 1,000,000 default rejects real buildings as corrupt.
	// A table costs at least a 4-byte offset, so the buffer itself is the honest bound.
	flatbuffers::Verifier::Options VerifierOptions;
	VerifierOptions.max_tables = static_cast<flatbuffers::uoffset_t>(
		FMath::Clamp<int64>(BufferSize / 4, 1000000, 100000000));

	flatbuffers::Verifier Verifier(Buffer, static_cast<size_t>(BufferSize), VerifierOptions);
	if (!Verifier.VerifyBuffer<Model>(nullptr))
	{
		FFragImportResult Result;
		Result.bSuccess = false;
		Result.ErrorMessage = TEXT("FlatBuffers verification failed — invalid or corrupt .frag data");
		UE_LOG(LogFragmentsUE, Error, TEXT("%s"), *Result.ErrorMessage);
		return Result;
	}

	return ParseModel(Buffer, BufferSize, Options);
}

#include <zlib.h>

// ─────────────────────────────────────────────────────────────────────────────
// Decompression
// ─────────────────────────────────────────────────────────────────────────────

bool FFragParser::TryDecompress(const TArray<uint8>& Data, TArray<uint8>& Out)
{
	if (Data.Num() < 2)
		return false;

	if (Data[0] != 0x78)
		return false;

	// A zlib header is valid when the 16-bit CMF/FLG word divides by 31 — not when
	// FLG is one of a few familiar values. The old whitelist missed 0x5E, which is
	// what levels 2-5 emit, so those files were rejected as corrupt.
	if ((((static_cast<uint32>(Data[0]) << 8) | Data[1]) % 31) != 0)
		return false;

	// FDICT: a preset dictionary we have no way to supply.
	if ((Data[1] & 0x20) != 0)
		return false;

	z_stream Stream = {};
	Stream.next_in = (Bytef*)Data.GetData();
	Stream.avail_in = Data.Num();

	if (inflateInit(&Stream) != Z_OK)
	{
		UE_LOG(LogFragmentsUE, Error, TEXT("zlib inflateInit failed"));
		return false;
	}

	// A DEFLATE stream expands by up to about 1032:1, so the size of `Out` is set by
	// the file's contents, not by its length on disk. Without a ceiling the append
	// loop below runs until FMemory raises a fatal out-of-memory error, which kills
	// the process rather than failing the import. The allowance is generous next to
	// any real .frag — those compress a few times over, not sixty — and stays under
	// the 2 GiB that TArray's int32 count could not address anyway.
	constexpr int64 MinInflateAllowance = 256LL * 1024 * 1024;
	constexpr int64 MaxInflateAllowance = 1536LL * 1024 * 1024;
	const int64 InflateAllowance = FMath::Clamp<int64>(
		static_cast<int64>(Data.Num()) * 64, MinInflateAllowance, MaxInflateAllowance);

	// Inflate in chunks
	const int32 ChunkSize = 1024 * 1024; // 1 MB chunks
	// Data.Num() * 4 is an int32 product that wraps negative past a 512 MB input.
	Out.Empty(static_cast<int32>(FMath::Min<int64>(static_cast<int64>(Data.Num()) * 4, InflateAllowance)));

	TArray<uint8> ChunkBuffer;
	ChunkBuffer.SetNumUninitialized(ChunkSize);

	int64 TotalInflated = 0;
	int32 Ret;
	do
	{
		Stream.next_out = ChunkBuffer.GetData();
		Stream.avail_out = ChunkSize;

		Ret = inflate(&Stream, Z_NO_FLUSH);

		if (Ret == Z_STREAM_ERROR || Ret == Z_NEED_DICT || Ret == Z_DATA_ERROR || Ret == Z_MEM_ERROR)
		{
			UE_LOG(LogFragmentsUE, Error, TEXT("zlib inflate failed with code %d"), Ret);
			inflateEnd(&Stream);
			Out.Empty();
			return false;
		}

		int32 BytesInflated = ChunkSize - Stream.avail_out;
		if (BytesInflated > 0)
		{
			TotalInflated += BytesInflated;
			if (TotalInflated > InflateAllowance)
			{
				UE_LOG(LogFragmentsUE, Error,
					TEXT("Refusing to decompress further: %.1f MB of input has expanded past the %.1f MB ceiling. ")
					TEXT("The file is corrupt or is a decompression bomb."),
					Data.Num() / (1024.0 * 1024.0), InflateAllowance / (1024.0 * 1024.0));
				inflateEnd(&Stream);
				Out.Empty();
				return false;
			}

			Out.Append(ChunkBuffer.GetData(), BytesInflated);
		}
	} while (Stream.avail_out == 0);

	inflateEnd(&Stream);

	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// IFC Metadata
//
// Attributes and relations are stored as JSON tuples, one string per entry:
//     ["Name","Basic Wall:Generic - 200mm:150702","IFCLABEL"]   (attribute)
//     ["IsDefinedBy",347831,347833,347835]                      (relation)
//
// Attributes are parallel to local_ids (indexed by dense local id), while
// relations are keyed by relations_items[k] — the *express id* of the owner.
// Every id appearing inside a relation tuple is an express id too, so they are
// resolved back to dense local ids here.
// ─────────────────────────────────────────────────────────────────────────────

namespace
{
	/**
	 * Ceiling on how many items one model may declare. Each costs about 232 bytes of
	 * FFragItemMetadata against 4 bytes in the file, and the verifier bounds only the
	 * latter. The largest published IFC models run to a few million entities.
	 */
	constexpr uint32 MaxModelItems = 8 * 1000 * 1000;

	/** An IfcGloballyUniqueId is 22 characters; the longest IFC class name is 45. */
	constexpr uint32 MaxGuidBytes = 64;
	constexpr uint32 MaxCategoryBytes = 64;

	/** The model header is a STEP header and a little JSON — kilobytes, not megabytes. */
	constexpr uint32 MaxMetadataBytes = 1024 * 1024;

	/**
	 * One metadata tuple. The longest legitimate one is a storey's ContainsElements
	 * listing every element on the floor, which runs to hundreds of kilobytes.
	 */
	constexpr uint32 MaxTupleBytes = 4 * 1024 * 1024;

	/** Entries read from one item's attribute list outside the main metadata pass. */
	constexpr uint32 MaxItemAttributes = 256;

	/**
	 * Copy a string out of the file, refusing to touch more than MaxBytes of it.
	 *
	 * The length has to be read before the conversion, not after. UTF8_TO_TCHAR expands
	 * the whole string before any Left() could trim it, and FlatBuffers lets any number
	 * of offsets point at one string — so a single huge string can be converted in full
	 * once per referencing offset. That is time as much as memory, and no amount of
	 * trimming afterwards gets it back.
	 *
	 * Over-long input is refused rather than truncated. At these limits a value that is
	 * too long is not a valid one with something on the end; it is the wrong kind of
	 * thing, and keeping half of it would only make it look valid.
	 */
	FString ReadBoundedString(const flatbuffers::String* In, uint32 MaxBytes, int32* InOutRejected = nullptr)
	{
		if (!In)
		{
			return FString();
		}

		if (In->size() > MaxBytes)
		{
			if (InOutRejected)
			{
				(*InOutRejected)++;
			}
			return FString();
		}

		return FString(UTF8_TO_TCHAR(In->c_str()));
	}
}

namespace FragMetadata
{
	/**
	 * Split one JSON tuple into its elements.
	 * Quoted strings are unescaped; numbers, booleans and nested arrays/objects
	 * are returned verbatim; JSON null becomes an empty string.
	 */
	// One tuple cannot hold more elements than this. The longest legitimate one is a
	// storey's ContainsElements listing every element on the floor, which runs to tens
	// of thousands; past a million the string is not a relation tuple.
	constexpr int32 MaxTupleTokens = 1024 * 1024;

	static void SplitTuple(const FString& In, TArray<FString>& Out)
	{
		Out.Reset();

		const int32 Len = In.Len();
		int32 i = 0;

		while (i < Len && In[i] != TEXT('[')) i++;
		if (i >= Len) return;
		i++; // step past '['

		while (i < Len)
		{
			if (Out.Num() >= MaxTupleTokens) break;

			while (i < Len && (FChar::IsWhitespace(In[i]) || In[i] == TEXT(','))) i++;
			if (i >= Len || In[i] == TEXT(']')) break;

			if (In[i] == TEXT('"'))
			{
				i++; // step past the opening quote
				FString Token;
				while (i < Len)
				{
					const TCHAR C = In[i];

					if (C == TEXT('\\') && i + 1 < Len)
					{
						const TCHAR Escaped = In[i + 1];
						switch (Escaped)
						{
						case TEXT('n'): Token.AppendChar(TEXT('\n')); break;
						case TEXT('t'): Token.AppendChar(TEXT('\t')); break;
						case TEXT('r'): Token.AppendChar(TEXT('\r')); break;
						case TEXT('b'): Token.AppendChar(TEXT('\b')); break;
						case TEXT('f'): Token.AppendChar(TEXT('\f')); break;
						case TEXT('u'):
							if (i + 5 < Len)
							{
								const FString Hex = In.Mid(i + 2, 4);
								Token.AppendChar(static_cast<TCHAR>(FCString::Strtoi(*Hex, nullptr, 16)));
								i += 4;
							}
							break;
						default: Token.AppendChar(Escaped); break;
						}
						i += 2;
						continue;
					}

					if (C == TEXT('"'))
					{
						i++;
						break;
					}

					Token.AppendChar(C);
					i++;
				}
				Out.Add(MoveTemp(Token));
			}
			else
			{
				// Number, boolean, null, or a nested array/object — keep the raw text.
				const int32 Start = i;
				int32 Depth = 0;
				bool bInString = false;

				while (i < Len)
				{
					const TCHAR C = In[i];

					if (bInString)
					{
						if (C == TEXT('\\')) { i += 2; continue; }
						if (C == TEXT('"')) bInString = false;
					}
					else if (C == TEXT('"'))
					{
						bInString = true;
					}
					else if (C == TEXT('[') || C == TEXT('{'))
					{
						Depth++;
					}
					else if (C == TEXT('}'))
					{
						Depth--;
					}
					else if (C == TEXT(']'))
					{
						if (Depth == 0) break;
						Depth--;
					}
					else if (C == TEXT(',') && Depth == 0)
					{
						break;
					}

					i++;
				}

				FString Token = In.Mid(Start, i - Start).TrimStartAndEnd();
				if (Token.Equals(TEXT("null"), ESearchCase::IgnoreCase))
				{
					Token.Empty();
				}
				Out.Add(MoveTemp(Token));
			}
		}
	}

	// Property text is copied out of the file verbatim and re-copied into every item
	// that references the property, so it is clamped at the copy rather than after it:
	// a value read once per referencing item must never be duplicated at full length
	// first. Names and types are IfcLabel / IfcIdentifier, which the IFC schema caps at
	// 255 characters, so no schema-valid name is clipped.
	constexpr int32 MaxPropertyNameChars = 256;

	// A NominalValue may be IfcText, which the schema leaves unbounded: a Description,
	// Keynote or specification value is legitimately a paragraph, so this is far looser
	// than the name clamp and still bounds what one property can cost per reference.
	constexpr int32 MaxPropertyValueChars = 4096;

	// A type object's own attributes are copied into every occurrence of that type, so
	// the count is clamped as well as the text. Tag, PredefinedType and AssemblyPlace are
	// the usual three; nothing real approaches this.
	constexpr int32 MaxTypeAttributes = 256;

	/** Value carried by an IfcPropertySingleValue / IfcQuantity item. */
	static void ReadPropertyValue(const FFragItemMetadata& Property, FFragAttribute& OutProperty)
	{
		OutProperty.Name = (Property.Name.IsEmpty() ? Property.Category : Property.Name).Left(MaxPropertyNameChars);

		// The value lives in whichever attribute is not the property's own name —
		// NominalValue for IfcPropertySingleValue, LengthValue/AreaValue/... for quantities.
		for (const FFragAttribute& Candidate : Property.Attributes)
		{
			if (Candidate.Name.Equals(TEXT("Name"), ESearchCase::IgnoreCase))
			{
				continue;
			}
			OutProperty.Value = Candidate.Value.Left(MaxPropertyValueChars);
			OutProperty.Type = Candidate.Type.Left(MaxPropertyNameChars);
			break;
		}
	}

	/**
	 * Fill Result.Items with one record per item in the file: identity, direct
	 * attributes, relations, and (optionally) resolved property sets.
	 */
	static void BuildItemMetadata(
		const Model* InModel,
		const TArray<uint32>& LocalIds,
		const TMap<uint32, int32>& ExpressIdToDenseIndex,
		const TMap<int32, FString>& LocalIdToGuid,
		const FFragImportOptions& Options,
		int32 BufferSize,
		FFragImportResult& Result)
	{
		const int32 NumItems = LocalIds.Num();
		if (NumItems == 0)
		{
			UE_LOG(LogFragmentsUE, Warning, TEXT("Metadata: model has no local_ids, skipping"));
			return;
		}

		Result.Items.SetNum(NumItems);

		// ── 1. Identity + direct attributes ──
		const auto* Attributes = InModel->attributes();
		TArray<FString> Tokens;
		int32 AttributeCount = 0;
		int32 TruncatedAttributeItems = 0;
		int32 RejectedTuples = 0;

		// One tuple string, and the total across the pass. The per-tuple cap alone
		// bounds nothing in aggregate: the attributes vector is a vector of tables, so
		// a million of its offsets may point at one table, whose own entries may point
		// at one large string — and each visit expands it in full whether or not the
		// result is kept. The budget is what makes the conversion work linear.
		//
		// Scaled from the buffer rather than fixed, because a fixed ceiling truncates
		// real data: Result.Items holds one record per IFC *entity*, not per element —
		// property sets, properties, types and spatial nodes are all entities — so a
		// 300k-element building runs to millions of items and can legitimately carry
		// most of a gigabyte of tuple text. A real file's distinct tuple bytes cannot
		// exceed its own decompressed size, so scaling from that never truncates an
		// honest model while still holding aliasing to a small multiple.
		int64 TupleByteBudget = FMath::Max(256LL * 1024 * 1024, 4LL * BufferSize);
		bool bTupleBudgetSpent = false;

		// Reads one tuple string if it is affordable, and reports whether it was.
		auto ReadTuple = [&](const flatbuffers::String* TupleString) -> bool
		{
			if (!TupleString || TupleString->size() > MaxTupleBytes)
			{
				RejectedTuples++;
				return false;
			}
			if (bTupleBudgetSpent)
			{
				return false;
			}

			TupleByteBudget -= TupleString->size();
			if (TupleByteBudget <= 0)
			{
				bTupleBudgetSpent = true;
				return false;
			}

			SplitTuple(UTF8_TO_TCHAR(TupleString->c_str()), Tokens);
			return true;
		};

		for (int32 ItemIdx = 0; ItemIdx < NumItems; ItemIdx++)
		{
			if (bTupleBudgetSpent)
			{
				break;
			}

			FFragItemMetadata& Item = Result.Items[ItemIdx];
			Item.LocalId = ItemIdx;
			Item.ExpressId = static_cast<int64>(LocalIds[ItemIdx]);

			if (Result.Categories.IsValidIndex(ItemIdx))
			{
				Item.Category = Result.Categories[ItemIdx];
			}
			if (const FString* Guid = LocalIdToGuid.Find(ItemIdx))
			{
				Item.GUID = *Guid;
			}

			if (!Attributes || static_cast<uint32>(ItemIdx) >= Attributes->size())
			{
				continue;
			}

			const Attribute* AttributeEntry = Attributes->Get(static_cast<uint32>(ItemIdx));
			if (!AttributeEntry || !AttributeEntry->data())
			{
				continue;
			}

			const auto* Data = AttributeEntry->data();

			// The vector holds string *offsets*, and several of them may point at the
			// same string, so its length bounds neither the bytes read nor the memory
			// written: a million offsets onto one large string cost four bytes each in
			// the file and a full copy each here. Reserve for a plausible entity rather
			// than for the claim, and stop at a count no IFC entity comes near — the
			// schema fixes an entity's attribute list, and the longest run to about
			// twenty.
			const uint32 AttributeEntryCount = FMath::Min(Data->size(), MaxItemAttributes);
			if (Data->size() > MaxItemAttributes)
			{
				TruncatedAttributeItems++;
			}
			Item.Attributes.Reserve(AttributeEntryCount);

			for (uint32 j = 0; j < AttributeEntryCount; j++)
			{
				if (!ReadTuple(Data->Get(j)) || Tokens.Num() == 0)
				{
					continue;
				}

				// Clamped at the copy for the same reason property text is: one huge
				// string in the file must not become one huge string per item holding it.
				FFragAttribute NewAttribute;
				NewAttribute.Name = Tokens[0].Left(MaxPropertyNameChars);
				if (Tokens.Num() > 1) NewAttribute.Value = Tokens[1].Left(MaxPropertyValueChars);
				if (Tokens.Num() > 2) NewAttribute.Type = Tokens[2].Left(MaxPropertyNameChars);

				if (Item.Name.IsEmpty() && NewAttribute.Name.Equals(TEXT("Name"), ESearchCase::IgnoreCase))
				{
					Item.Name = NewAttribute.Value;
				}

				Item.Attributes.Add(MoveTemp(NewAttribute));
				AttributeCount++;
			}

			// The reserve above is sized from the claimed entry count, which costs
			// nothing to inflate: tuples that split to no tokens add nothing but leave
			// their capacity behind, and TArray never gives it back on its own. At 48
			// bytes per slot that is 12 KB per item held for the life of the import.
			if (Item.Attributes.Max() > Item.Attributes.Num() * 2)
			{
				Item.Attributes.Shrink();
			}
		}

		if (TruncatedAttributeItems > 0)
		{
			UE_LOG(LogFragmentsUE, Warning,
				TEXT("%d item(s) declared more than %u direct attributes; the rest were dropped."),
				TruncatedAttributeItems, MaxItemAttributes);
		}

		// ── 2. Relations ──
		int32 RelationCount = 0;
		int32 TruncatedRelationItems = 0;
		if (InModel->relations() && InModel->relations_items())
		{
			const auto* Relations = InModel->relations();
			const auto* RelationItems = InModel->relations_items();
			const uint32 PairCount = FMath::Min(Relations->size(), RelationItems->size());

			for (uint32 k = 0; k < PairCount; k++)
			{
				if (bTupleBudgetSpent)
				{
					break;
				}

				const int32 OwnerExpressId = RelationItems->Get(k);
				if (OwnerExpressId < 0)
				{
					continue;
				}

				const int32* OwnerDenseIdx = ExpressIdToDenseIndex.Find(static_cast<uint32>(OwnerExpressId));
				if (!OwnerDenseIdx || !Result.Items.IsValidIndex(*OwnerDenseIdx))
				{
					continue;
				}

				const Relation* RelationEntry = Relations->Get(k);
				if (!RelationEntry || !RelationEntry->data())
				{
					continue;
				}

				FFragItemMetadata& Owner = Result.Items[*OwnerDenseIdx];
				const auto* Data = RelationEntry->data();

				// The cap has to be per item, not per entry. relations_items is a plain
				// int32 vector, so any number of its entries may name the same owner, and
				// the relations vector may point every one of its offsets at a single
				// table — bounding one table's tuple list therefore bounds nothing at all.
				// An element carries one entry per inverse relation type, which the IFC
				// schema keeps well under twenty.
				constexpr int32 MaxItemRelations = 256;
				const int32 RelationHeadroom = MaxItemRelations - Owner.Relations.Num();
				if (RelationHeadroom <= 0)
				{
					TruncatedRelationItems++;
					continue;
				}

				const uint32 RelationEntryCount =
					FMath::Min(Data->size(), static_cast<uint32>(RelationHeadroom));

				for (uint32 j = 0; j < RelationEntryCount; j++)
				{
					if (!ReadTuple(Data->Get(j)) || Tokens.Num() == 0)
					{
						continue;
					}

					// A storey listing every element on the floor is the longest real
					// relation, at tens of thousands of ids. The reserve is taken before
					// a single id has been validated and the array is then kept, so it
					// has to be sized against what is plausible rather than what the
					// tuple claims.
					constexpr int32 MaxRelationTargets = 65536;
					const int32 TargetCount = FMath::Min(Tokens.Num() - 1, MaxRelationTargets);

					FFragRelation NewRelation;
					NewRelation.Name = Tokens[0].Left(MaxPropertyNameChars);
					NewRelation.RelatedLocalIds.Reserve(TargetCount);

					for (int32 t = 1; t <= TargetCount; t++)
					{
						const int64 TargetExpressId = FCString::Atoi64(*Tokens[t]);
						if (TargetExpressId <= 0 || TargetExpressId > MAX_uint32)
						{
							continue;
						}
						if (const int32* TargetDenseIdx = ExpressIdToDenseIndex.Find(static_cast<uint32>(TargetExpressId)))
						{
							NewRelation.RelatedLocalIds.Add(*TargetDenseIdx);
						}
					}

					// Ids that resolve to nothing are dropped, so the reserve above is an
					// upper bound rather than the final size — and this array is kept.
					if (NewRelation.RelatedLocalIds.Max() > NewRelation.RelatedLocalIds.Num() * 2)
					{
						NewRelation.RelatedLocalIds.Shrink();
					}

					Owner.Relations.Add(MoveTemp(NewRelation));
					RelationCount++;
				}
			}

			if (TruncatedRelationItems > 0)
			{
				UE_LOG(LogFragmentsUE, Warning,
					TEXT("%d relation entr(ies) went past the 256-relation limit for one item; the rest were dropped."),
					TruncatedRelationItems);
			}
		}

		if (RejectedTuples > 0)
		{
			UE_LOG(LogFragmentsUE, Warning,
				TEXT("%d metadata tuple(s) were larger than %u bytes and were skipped."),
				RejectedTuples, MaxTupleBytes);
		}
		if (bTupleBudgetSpent)
		{
			UE_LOG(LogFragmentsUE, Error,
				TEXT("Metadata text allowance spent; the remaining attributes and relations were not read."));
		}

		// ── 3. Property sets ──
		// element --IsDefinedBy--> IfcPropertySet --HasProperties--> IfcPropertySingleValue
		// element --IsDefinedBy--> IfcTypeObject  --HasPropertySets--> IfcPropertySet --> ...
		int32 PropertySetCount = 0;
		if (Options.bImportPropertySets)
		{
			const FString RelIsDefinedBy(TEXT("IsDefinedBy"));
			const FString RelHasPropertySets(TEXT("HasPropertySets"));
			const FString RelHasProperties(TEXT("HasProperties"));
			const FString RelQuantities(TEXT("Quantities"));

			// One unit is charged per related id examined, and a second when that id is
			// walked into, so a single budget bounds both the walk and the rows it emits:
			// property rows never exceed units spent. A Revit-exported wall — Pset_WallCommon
			// (11), Qto_WallBaseQuantities (9) and six Pset_Revit_* parameter groups — spends
			// about 90. The fattest legitimate element, an MEP occurrence whose type carries a
			// full performance schedule plus COBie, runs to roughly 30 sets and 1,500 units.
			constexpr int32 MaxPropertyUnitsPerItem = 8192;

			// Visited is per item because a type's property sets belong in every occurrence of
			// that type — a sharing factor of 100+ is correct BIM, not an attack — so the
			// per-item cap alone leaves the model-wide total at NumItems x set size. This
			// grants 512 emitted rows per item in the file: a Revit export writes most sets per
			// occurrence and lands near 3, and only a file whose elements draw hundreds of
			// properties each from a handful of shared types approaches it. The 256k clamp
			// holds the model-wide ceiling at 537M rows, which a 300k-element hospital (12-16M
			// items, 18-36M rows) clears by 15x and a 1M-element plant still clears, and keeps
			// the product inside int32. Row count is bounded here; bytes per row are bounded by
			// the text clamps and the charges that price them.
			const int32 MaxPropertyUnitsPerModel = FMath::Min(NumItems, 1024 * 1024) * 512;
			int32 ModelBudget = MaxPropertyUnitsPerModel;
			int32 BudgetExhaustedItems = 0;
			int32 PropertyCandidateItems = 0;

			TFunction<void(int32, int32, bool, TArray<FFragPropertySet>&, TSet<int32>&, int32&)> GatherSets;
			GatherSets = [&](int32 TargetId, int32 Depth, bool bFromType, TArray<FFragPropertySet>& OutSets, TSet<int32>& Visited, int32& Budget)
			{
				if (Depth > 2 || Budget <= 0 || !Result.Items.IsValidIndex(TargetId) || Visited.Contains(TargetId))
				{
					return;
				}
				Visited.Add(TargetId);
				Budget--;

				const FFragItemMetadata& Target = Result.Items[TargetId];

				// Both loops below scan this node's whole relation list, and that list is
				// re-scanned for every item that reaches this node, so a node large enough to
				// make the scan expensive pays before it is scanned at all. A real property
				// set — one or two relations and a name of a few dozen characters — bills
				// nothing. Summed in 64-bit and clamped, because a negative charge would hand
				// budget back.
				const int64 NodeSize = static_cast<int64>(Target.Relations.Num()) + Target.Name.Len() + Target.Category.Len();
				if (NodeSize >= 64)
				{
					Budget -= static_cast<int32>(FMath::Min<int64>(Budget, NodeSize / 64));
					if (Budget <= 0)
					{
						return;
					}
				}

				bool bIsPropertySet = false;

				// HasProperties and Quantities are SETs in the schema, so a repeated id is
				// padding or corruption either way: emitting it twice duplicates a row in
				// the Details panel and multiplies whatever the file points at. Scoped to the
				// node, so repeating an id across two of its relations does not slip through.
				TSet<int32> SeenProperties;

				for (const FFragRelation& RelationRef : Target.Relations)
				{
					// Charge per relation examined, not per set emitted: the two name compares
					// below are the scan the charge above only amortises.
					if (--Budget <= 0)
					{
						break;
					}

					if (RelationRef.Name != RelHasProperties && RelationRef.Name != RelQuantities)
					{
						continue;
					}
					bIsPropertySet = true;

					FFragPropertySet NewSet;
					NewSet.Name = (Target.Name.IsEmpty() ? Target.Category : Target.Name).Left(MaxPropertyNameChars);
					NewSet.LocalId = TargetId;
					NewSet.bFromType = bFromType;

					// The node's own size is charged once per visit, but this name is copied
					// once per relation on it and again for every item that reaches it, so the
					// copy pays for itself. Break rather than continue: the half-built set is
					// discarded instead of emitted for free.
					const int32 SetNameCharge = NewSet.Name.Len() / 64;
					if (SetNameCharge > 0)
					{
						Budget -= FMath::Min(Budget, SetNameCharge);
						if (Budget <= 0)
						{
							break;
						}
					}

					// Reserve what the budget can still pay for, never the id count: the list
					// length is file-controlled and one FFragAttribute costs 48 bytes before
					// any of its three strings is filled in.
					NewSet.Properties.Reserve(FMath::Min(RelationRef.RelatedLocalIds.Num(), Budget));

					for (int32 PropertyId : RelationRef.RelatedLocalIds)
					{
						// Charge per id, not per row: a list padded with one repeated or invalid
						// id must cost budget instead of running to its end for free.
						if (--Budget <= 0)
						{
							break;
						}

						if (!Result.Items.IsValidIndex(PropertyId))
						{
							continue;
						}

						bool bAlreadySeen = false;
						SeenProperties.Add(PropertyId, &bAlreadySeen);
						if (bAlreadySeen)
						{
							continue;
						}

						// ReadPropertyValue scans this property's attribute list for the first
						// entry that is not its name, and that scan runs again for every item
						// sharing the property. A real property carries two or three attributes.
						const FFragItemMetadata& PropertyItem = Result.Items[PropertyId];
						if (PropertyItem.Attributes.Num() >= 64)
						{
							Budget -= FMath::Min(Budget, PropertyItem.Attributes.Num() / 64);
							if (Budget <= 0)
							{
								break;
							}
						}

						FFragAttribute NewProperty;
						ReadPropertyValue(PropertyItem, NewProperty);
						if (NewProperty.Name.IsEmpty())
						{
							continue;
						}

						// Clamping bounds one row; this bounds their sum, because one shared
						// property's text is copied again into every item that references it.
						// A real row — "LoadBearing", "true", "IFCBOOLEAN" — bills nothing; a
						// row padded to the clamps bills 24.
						const int32 TextCharge = (NewProperty.Name.Len() + NewProperty.Value.Len() + NewProperty.Type.Len()) / 64;
						if (TextCharge > 0)
						{
							Budget -= FMath::Min(Budget, TextCharge);
						}

						NewSet.Properties.Add(MoveTemp(NewProperty));
					}

					if (NewSet.Properties.Num() > 0)
					{
						// The reservation above is sized before any id is validated or deduped,
						// and TArray never gives capacity back on its own.
						if (NewSet.Properties.Max() > NewSet.Properties.Num() * 2)
						{
							NewSet.Properties.Shrink();
						}
						OutSets.Add(MoveTemp(NewSet));
					}
				}

				if (bIsPropertySet)
				{
					return;
				}

				// Not a property set itself — most likely an IfcTypeObject, so
				// pick up the property sets it defines for its occurrences.
				for (const FFragRelation& RelationRef : Target.Relations)
				{
					if (--Budget <= 0)
					{
						break;
					}

					if (RelationRef.Name != RelHasPropertySets)
					{
						continue;
					}
					for (int32 ChildId : RelationRef.RelatedLocalIds)
					{
						// Charge per id, not per visit: the Visited guard returns in O(1), so a
						// list padded with one repeated id would otherwise be walked for free.
						if (--Budget <= 0)
						{
							break;
						}

						GatherSets(ChildId, Depth + 1, /*bFromType*/ true, OutSets, Visited, Budget);
					}
				}
			};

			for (int32 ItemIdx = 0; ItemIdx < NumItems; ItemIdx++)
			{
				FFragItemMetadata& Item = Result.Items[ItemIdx];
				if (Item.Relations.Num() == 0)
				{
					continue;
				}

				// Visited stays per item because the same type's sets belong in every
				// occurrence of that type. NodeBudget is drawn from the model budget and
				// settled back below, so that sharing cannot expand into an unbounded
				// model-wide total while a single element still gets its full cap.
				TSet<int32> Visited;
				Visited.Add(ItemIdx);
				int32 NodeBudget = FMath::Min(MaxPropertyUnitsPerItem, ModelBudget);
				const int32 StartBudget = NodeBudget;
				bool bWantedProperties = false;

				for (const FFragRelation& RelationRef : Item.Relations)
				{
					if (RelationRef.Name != RelIsDefinedBy && RelationRef.Name != RelHasPropertySets)
					{
						continue;
					}
					bWantedProperties = true;

					const bool bFromType = (RelationRef.Name == RelHasPropertySets);
					for (int32 TargetId : RelationRef.RelatedLocalIds)
					{
						// Charge per id, not per visit: a target list padded with one repeated
						// id must cost budget instead of running to its end for free.
						if (--NodeBudget <= 0)
						{
							break;
						}

						GatherSets(TargetId, 1, bFromType, Item.PropertySets, Visited, NodeBudget);
					}

					if (NodeBudget <= 0)
					{
						break;
					}
				}

				// A node charge can overshoot, so settle at most what this item was granted.
				ModelBudget = FMath::Max(0, ModelBudget - FMath::Clamp(StartBudget - NodeBudget, 0, StartBudget));

				// Also counted when the pool was already empty and this item was granted
				// nothing: an item skipped that way loses its properties just as silently.
				// Only items that actually asked for property sets can have lost any.
				if (bWantedProperties)
				{
					PropertyCandidateItems++;
					if (NodeBudget <= 0)
					{
						BudgetExhaustedItems++;
					}
				}

				PropertySetCount += Item.PropertySets.Num();
			}

			// Truncated BIM metadata must never be silent. Reported against the items that
			// actually carry property relations, not every item in the file.
			if (BudgetExhaustedItems > 0)
			{
				UE_LOG(LogFragmentsUE, Warning,
					TEXT("Metadata: the property set walk ran out of budget on %d of %d items with property relations; some properties were not imported"),
					BudgetExhaustedItems, PropertyCandidateItems);
			}
		}

		// ── 4. Materials, classifications, type object, spatial container ──
		// element --HasAssociations--> IfcMaterial
		//                          |-> IfcMaterialList        --Materials-->      IfcMaterial
		//                          |-> IfcMaterialLayerSet    --MaterialLayers--> IfcMaterialLayer --Material--> IfcMaterial
		//                          |-> IfcMaterialLayerSetUsage --ForLayerSet-->  IfcMaterialLayerSet --> ...
		//                          '-> IfcClassificationReference
		int32 MaterialCount = 0;
		int32 ContainerCount = 0;

		{
			const FString RelHasAssociations(TEXT("HasAssociations"));
			const FString RelMaterials(TEXT("Materials"));
			const FString RelForLayerSet(TEXT("ForLayerSet"));
			const FString RelMaterialLayers(TEXT("MaterialLayers"));
			const FString RelMaterial(TEXT("Material"));
			const FString RelContainedInStructure(TEXT("ContainedInStructure"));
			const FString RelDecomposes(TEXT("Decomposes"));
			const FString RelObjectTypeOf(TEXT("ObjectTypeOf"));
			const FString RelIsDefinedByType(TEXT("IsDefinedBy"));

			auto FindRelation = [&Result](int32 ItemId, const FString& RelationName) -> const FFragRelation*
			{
				if (const FFragItemMetadata* Found = Result.FindItem(ItemId))
				{
					for (const FFragRelation& Candidate : Found->Relations)
					{
						if (Candidate.Name == RelationName)
						{
							return &Candidate;
						}
					}
				}
				return nullptr;
			};

			auto AttributeValue = [&Result](int32 ItemId, const TCHAR* AttributeName) -> FString
			{
				if (const FFragItemMetadata* Found = Result.FindItem(ItemId))
				{
					if (const FFragAttribute* Attr = Found->FindAttribute(AttributeName))
					{
						return Attr->Value;
					}
				}
				return FString();
			};

			// One unit is charged per related id examined, and a second when that id is
			// recursed into, so a single budget bounds both the walk and the rows it emits:
			// rows never exceed units spent. A three-layer wall spends about 10; the worst
			// legitimate shape, a 20-layer curtain wall with a few classification refs,
			// spends about 50.
			constexpr int32 MaxAssociationNodesPerItem = 512;

			// Names and layer set names are copied into every row they appear in, so they are
			// clamped: an IFC material name runs to a few dozen characters, and without a
			// bound one huge string is re-copied into every item that references it.
			constexpr int32 MaxAssociationTextChars = 256;

			// Visited is per item because one layer set is legitimately re-walked for every
			// element that uses it, so the totals need a model-wide ceiling as well. 128 per
			// item clears the densest legal file - every element a shared 20-layer wall, about
			// 44 units per item. Every material, layer and classification is itself an item, so
			// a 300k-element model holds millions: the budget has to keep scaling well past
			// that, and only stops at 1M items so the product cannot overflow int32.
			const int32 MaxAssociationNodesPerModel = FMath::Min(NumItems, 1024 * 1024) * 128;
			int32 ModelBudget = MaxAssociationNodesPerModel;
			int32 BudgetExhaustedItems = 0;

			TFunction<void(int32, const FString&, int32, FFragItemMetadata&, TSet<int32>&, int32&)> AddAssociation;
			AddAssociation = [&](int32 TargetId, const FString& LayerSetName, int32 Depth, FFragItemMetadata& Item, TSet<int32>& Visited, int32& Budget)
			{
				const FFragItemMetadata* Target = Result.FindItem(TargetId);
				if (!Target || Depth > 4 || Budget <= 0 || Visited.Contains(TargetId))
				{
					return;
				}
				Visited.Add(TargetId);
				Budget--;

				if (Target->Category.Equals(TEXT("IFCMATERIAL"), ESearchCase::IgnoreCase))
				{
					if (!Target->Name.IsEmpty())
					{
						FFragMaterial NewMaterial;
						NewMaterial.Name = Target->Name.Left(MaxAssociationTextChars);
						NewMaterial.LayerSetName = LayerSetName;
						NewMaterial.LocalId = TargetId;
						Item.Materials.Add(MoveTemp(NewMaterial));
					}
					return;
				}

				// The lookups below scan this node's attribute and relation lists and its
				// category, so a node large enough to make those scans expensive pays for
				// itself. A real node bills nothing: the largest legal shape is a handful of
				// attributes, one or two relations and a 26-character category name. Summed in
				// 64-bit and clamped, because a negative charge would hand budget back.
				const int64 NodeSize = static_cast<int64>(Target->Attributes.Num()) + Target->Relations.Num() + Target->Category.Len();
				if (NodeSize >= 64)
				{
					Budget -= static_cast<int32>(FMath::Min<int64>(Budget, NodeSize / 64));
					if (Budget <= 0)
					{
						return;
					}
				}

				if (Target->Category.Contains(TEXT("CLASSIFICATION"), ESearchCase::IgnoreCase))
				{
					FFragAttribute NewClassification;
					// IFC2X3 calls it ItemReference, IFC4 calls it Identification.
					NewClassification.Name = Target->FindAttribute(TEXT("Identification"))
						? Target->FindAttribute(TEXT("Identification"))->Value
						: (Target->FindAttribute(TEXT("ItemReference"))
							? Target->FindAttribute(TEXT("ItemReference"))->Value
							: Target->Category);
					NewClassification.Name.LeftInline(MaxAssociationTextChars);
					NewClassification.Value = Target->Name.Left(MaxAssociationTextChars);
					NewClassification.Type = Target->Category.Left(MaxAssociationTextChars);
					Item.Classifications.Add(MoveTemp(NewClassification));
					return;
				}

				// IfcMaterialList / IfcMaterialLayerSetUsage — step through to the real materials.
				if (const FFragRelation* ListRelation = FindRelation(TargetId, RelMaterials))
				{
					for (int32 ChildId : ListRelation->RelatedLocalIds)
					{
						if (--Budget <= 0)
						{
							break;
						}

						AddAssociation(ChildId, LayerSetName, Depth + 1, Item, Visited, Budget);
					}
				}
				if (const FFragRelation* UsageRelation = FindRelation(TargetId, RelForLayerSet))
				{
					for (int32 ChildId : UsageRelation->RelatedLocalIds)
					{
						if (--Budget <= 0)
						{
							break;
						}

						AddAssociation(ChildId, LayerSetName, Depth + 1, Item, Visited, Budget);
					}
				}

				// IfcMaterialLayerSet — each layer carries a thickness and one material.
				if (const FFragRelation* LayersRelation = FindRelation(TargetId, RelMaterialLayers))
				{
					FString SetName = AttributeValue(TargetId, TEXT("LayerSetName")).Left(MaxAssociationTextChars);
					if (SetName.IsEmpty())
					{
						SetName = LayerSetName;
					}

					for (int32 LayerId : LayersRelation->RelatedLocalIds)
					{
						if (--Budget <= 0)
						{
							break;
						}

						// A layer is read here without ever going through the entry charge above,
						// so the two lookups below would otherwise scan its lists for free.
						if (const FFragItemMetadata* LayerItem = Result.FindItem(LayerId))
						{
							const int64 LayerSize = static_cast<int64>(LayerItem->Attributes.Num()) + LayerItem->Relations.Num();
							if (LayerSize >= 64)
							{
								Budget -= static_cast<int32>(FMath::Min<int64>(Budget, LayerSize / 64));
								if (Budget <= 0)
								{
									break;
								}
							}
						}

						const float Thickness = FCString::Atof(*AttributeValue(LayerId, TEXT("LayerThickness")));
						const FFragRelation* MaterialRelation = FindRelation(LayerId, RelMaterial);
						if (!MaterialRelation)
						{
							continue;
						}

						for (int32 MaterialId : MaterialRelation->RelatedLocalIds)
						{
							if (--Budget <= 0)
							{
								break;
							}

							const FFragItemMetadata* MaterialItem = Result.FindItem(MaterialId);
							if (!MaterialItem || MaterialItem->Name.IsEmpty())
							{
								continue;
							}

							FFragMaterial NewMaterial;
							NewMaterial.Name = MaterialItem->Name.Left(MaxAssociationTextChars);
							NewMaterial.LayerSetName = SetName;
							NewMaterial.Thickness = Thickness;
							NewMaterial.LocalId = MaterialId;
							Item.Materials.Add(MoveTemp(NewMaterial));
						}
					}
				}
			};

			for (int32 ItemIdx = 0; ItemIdx < NumItems; ItemIdx++)
			{
				FFragItemMetadata& Item = Result.Items[ItemIdx];
				if (Item.Relations.Num() == 0)
				{
					continue;
				}

				// Relation targets come from the file and may repeat or form cycles, so one
				// Visited set per item bounds the walk and NodeBudget caps the rows it emits.
				// A real element stays under a few dozen: a layer set runs to about 20 layers.
				// NodeBudget is drawn from the model budget, so the shared subgraph a thousand
				// walls point at cannot be re-emitted without limit across the whole model.
				TSet<int32> Visited;
				Visited.Add(ItemIdx);
				int32 NodeBudget = FMath::Min(MaxAssociationNodesPerItem, ModelBudget);
				const int32 StartBudget = NodeBudget;

				for (const FFragRelation& RelationRef : Item.Relations)
				{
					if (RelationRef.Name != RelHasAssociations)
					{
						continue;
					}
					for (int32 TargetId : RelationRef.RelatedLocalIds)
					{
						// Charge per id, not per visit: a target list padded with one repeated
						// id must cost budget instead of running to its end for free.
						if (--NodeBudget <= 0)
						{
							break;
						}

						AddAssociation(TargetId, FString(), 0, Item, Visited, NodeBudget);
					}

					if (NodeBudget <= 0)
					{
						break;
					}
				}

				// A node charge can overshoot, so settle at most what this item was granted.
				ModelBudget = FMath::Max(0, ModelBudget - FMath::Clamp(StartBudget - NodeBudget, 0, StartBudget));
				if (NodeBudget <= 0 && NodeBudget < StartBudget)
				{
					BudgetExhaustedItems++;
				}
				MaterialCount += Item.Materials.Num();

				// Spatial container: prefer explicit containment, fall back to decomposition.
				for (const FFragRelation& RelationRef : Item.Relations)
				{
					const bool bIsContainment = (RelationRef.Name == RelContainedInStructure);
					if (!bIsContainment && RelationRef.Name != RelDecomposes)
					{
						continue;
					}
					if (RelationRef.RelatedLocalIds.Num() == 0)
					{
						continue;
					}
					if (Item.ContainerLocalId != INDEX_NONE && !bIsContainment)
					{
						continue;
					}

					const int32 ContainerId = RelationRef.RelatedLocalIds[0];
					if (const FFragItemMetadata* Container = Result.FindItem(ContainerId))
					{
						Item.ContainerLocalId = ContainerId;
						Item.ContainerName = Container->Name;
						Item.ContainerCategory = Container->Category;
					}
					if (bIsContainment)
					{
						break;
					}
				}
				if (Item.ContainerLocalId != INDEX_NONE)
				{
					ContainerCount++;
				}

				// Type object: an IsDefinedBy target that is a type rather than a property set.
				for (const FFragRelation& RelationRef : Item.Relations)
				{
					if (RelationRef.Name != RelIsDefinedByType)
					{
						continue;
					}
					for (int32 TargetId : RelationRef.RelatedLocalIds)
					{
						const FFragItemMetadata* Target = Result.FindItem(TargetId);
						if (!Target)
						{
							continue;
						}
						if (Target->Category.EndsWith(TEXT("TYPE"), ESearchCase::IgnoreCase)
							|| FindRelation(TargetId, RelObjectTypeOf))
						{
							Item.TypeName = Target->Name;
							Item.TypeLocalId = TargetId;

							// The type object carries attributes of its own —
							// Tag, PredefinedType, AssemblyPlace — that belong to
							// every occurrence but live nowhere else. The list comes
							// from the file and is copied into every occurrence of the
							// type, so it is clamped here the way the property set walk
							// clamps its own rows. A real type object carries a handful.
							if (Target->Attributes.Num() > 0)
							{
								FFragPropertySet TypeSet;
								TypeSet.Name = Target->Category.Left(MaxPropertyNameChars);
								TypeSet.LocalId = TargetId;
								TypeSet.bFromType = true;

								const int32 TypeAttributeCount = FMath::Min(Target->Attributes.Num(), MaxTypeAttributes);
								TypeSet.Properties.Reserve(TypeAttributeCount);
								for (int32 AttrIdx = 0; AttrIdx < TypeAttributeCount; AttrIdx++)
								{
									const FFragAttribute& Source = Target->Attributes[AttrIdx];
									FFragAttribute TypeProperty;
									TypeProperty.Name = Source.Name.Left(MaxPropertyNameChars);
									TypeProperty.Value = Source.Value.Left(MaxPropertyValueChars);
									TypeProperty.Type = Source.Type.Left(MaxPropertyNameChars);
									TypeSet.Properties.Add(MoveTemp(TypeProperty));
								}

								Item.PropertySets.Add(MoveTemp(TypeSet));
							}
							break;
						}
					}
					if (Item.TypeLocalId != INDEX_NONE)
					{
						break;
					}
				}
			}

			// Truncated BIM metadata must never be silent.
			if (BudgetExhaustedItems > 0)
			{
				UE_LOG(LogFragmentsUE, Warning,
					TEXT("Metadata: the association walk ran out of budget on %d of %d items; some materials or classifications were not imported"),
					BudgetExhaustedItems, NumItems);
			}

			// Second pass: walk the containment chain up to the building storey.
			// An element aggregated into another element (a door in a curtain wall)
			// has no direct storey, but its container's container eventually does.
			for (int32 ItemIdx = 0; ItemIdx < NumItems; ItemIdx++)
			{
				FFragItemMetadata& Item = Result.Items[ItemIdx];

				int32 Current = (Item.ContainerLocalId != INDEX_NONE) ? Item.ContainerLocalId : ItemIdx;
				for (int32 Step = 0; Step < 8 && Current != INDEX_NONE; Step++)
				{
					const FFragItemMetadata* Ancestor = Result.FindItem(Current);
					if (!Ancestor)
					{
						break;
					}
					if (Ancestor->Category.Equals(TEXT("IFCBUILDINGSTOREY"), ESearchCase::IgnoreCase))
					{
						Item.StoreyName = Ancestor->Name;
						Item.StoreyLocalId = Current;
						break;
					}
					Current = Ancestor->ContainerLocalId;
				}
			}
		}

	}

	/**
	 * Model-level header: IFC schema, authoring tool, project/site/building names
	 * and the unit assignment. Stored as a normal item record so it renders in the
	 * Details panel with the same layout as an element.
	 */
	static void BuildModelInfo(const FFragImportResult& Source, FFragItemMetadata& OutInfo)
	{
		OutInfo.LocalId = INDEX_NONE;
		OutInfo.Category = TEXT("IfcProject");
		OutInfo.Name = Source.ModelName;
		OutInfo.GUID = Source.ModelGuid;

		auto AddValue = [&OutInfo](const TCHAR* Name, const FString& Value)
		{
			if (!Value.IsEmpty())
			{
				OutInfo.Attributes.Add(FFragAttribute(Name, Value, FString()));
			}
		};

		// The .frag header mirrors the ISO-10303-21 STEP header:
		// names    = FILE_NAME(name, timestamp, author, organization, preprocessor, originating system, authorization)
		// descriptions = FILE_DESCRIPTION(description[], implementation level)
		if (!Source.Metadata.IsEmpty())
		{
			TSharedPtr<FJsonObject> Root;
			const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Source.Metadata);

			if (FJsonSerializer::Deserialize(Reader, Root) && Root.IsValid())
			{
				FString Schema;
				if (Root->TryGetStringField(TEXT("schema"), Schema))
				{
					AddValue(TEXT("IFC Schema"), Schema);
				}

				static const TCHAR* NameLabels[] = {
					TEXT("File Name"), TEXT("Exported"), TEXT("Author"), TEXT("Organization"),
					TEXT("Preprocessor"), TEXT("Authoring Tool"), TEXT("Authorization")
				};

				const TArray<TSharedPtr<FJsonValue>>* Names = nullptr;
				if (Root->TryGetArrayField(TEXT("names"), Names) && Names)
				{
					for (int32 i = 0; i < Names->Num(); i++)
					{
						const FString Value = (*Names)[i]->AsString();
						const TCHAR* Label = (i < UE_ARRAY_COUNT(NameLabels)) ? NameLabels[i] : TEXT("Header");
						AddValue(Label, Value);
					}
				}

				const TArray<TSharedPtr<FJsonValue>>* Descriptions = nullptr;
				if (Root->TryGetArrayField(TEXT("descriptions"), Descriptions) && Descriptions)
				{
					for (int32 i = 0; i < Descriptions->Num(); i++)
					{
						AddValue(TEXT("Description"), (*Descriptions)[i]->AsString());
					}
				}

				FString Crs;
				if (Root->TryGetStringField(TEXT("crs"), Crs))
				{
					AddValue(TEXT("Coordinate Reference"), Crs);
				}
			}
			else
			{
				AddValue(TEXT("Header"), Source.Metadata);
			}
		}

		// Project / site / building names, the postal address, and the units.
		FFragPropertySet Units;
		Units.Name = TEXT("Units");

		FFragPropertySet Address;
		Address.Name = TEXT("Address");

		for (const FFragItemMetadata& Item : Source.Items)
		{
			if (Item.Name.IsEmpty() && Item.Relations.Num() == 0)
			{
				continue;
			}

			if (Item.Category.Equals(TEXT("IFCPROJECT"), ESearchCase::IgnoreCase))
			{
				AddValue(TEXT("Project"), Item.Name);
			}
			else if (Item.Category.Equals(TEXT("IFCSITE"), ESearchCase::IgnoreCase))
			{
				AddValue(TEXT("Site"), Item.Name);
			}
			else if (Item.Category.Equals(TEXT("IFCBUILDING"), ESearchCase::IgnoreCase))
			{
				AddValue(TEXT("Building"), Item.Name);
			}

			// IfcPostalAddress on the building.
			if (Address.Properties.Num() == 0)
			{
				for (const FFragRelation& RelationRef : Item.Relations)
				{
					if (RelationRef.Name != TEXT("BuildingAddress"))
					{
						continue;
					}
					for (int32 AddressId : RelationRef.RelatedLocalIds)
					{
						if (const FFragItemMetadata* Postal = Source.FindItem(AddressId))
						{
							Address.Properties.Append(Postal->Attributes);
						}
					}
				}
			}

			if (Units.Properties.Num() > 0)
			{
				continue;
			}

			for (const FFragRelation& RelationRef : Item.Relations)
			{
				if (RelationRef.Name != TEXT("Units"))
				{
					continue;
				}

				for (int32 UnitId : RelationRef.RelatedLocalIds)
				{
					const FFragItemMetadata* Unit = Source.FindItem(UnitId);
					if (!Unit)
					{
						continue;
					}

					const FFragAttribute* UnitType = Unit->FindAttribute(TEXT("UnitType"));
					if (!UnitType)
					{
						continue;
					}

					// Derived units (density, moment of inertia, ...) carry no name of
					// their own — only named SI and conversion units are worth listing.
					if (Unit->Name.IsEmpty())
					{
						continue;
					}

					FString Value = Unit->Name;
					if (const FFragAttribute* Prefix = Unit->FindAttribute(TEXT("Prefix")))
					{
						Value = Prefix->Value + Value;
					}

					Units.Properties.Add(FFragAttribute(UnitType->Value, Value, Unit->Category));
				}
			}
		}

		if (Address.Properties.Num() > 0)
		{
			OutInfo.PropertySets.Add(MoveTemp(Address));
		}
		if (Units.Properties.Num() > 0)
		{
			OutInfo.PropertySets.Add(MoveTemp(Units));
		}
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// Core Parser
// ─────────────────────────────────────────────────────────────────────────────

FFragImportResult FFragParser::ParseModel(const uint8* Buffer, int32 BufferSize, const FFragImportOptions& Options)
{
	FFragImportResult Result;
	const float Scale = Options.ScaleFactor;

	const auto* Model = GetModel(Buffer);
	if (!Model)
	{
		Result.bSuccess = false;
		Result.ErrorMessage = TEXT("GetModel() returned null");
		return Result;
	}

	// Counts strings the file declared at a length that made them something other than
	// what they claim to be. Reported once at the end rather than per occurrence.
	int32 RejectedStrings = 0;

	// ── Model GUID ──
	Result.ModelGuid = ReadBoundedString(Model->guid(), MaxGuidBytes, &RejectedStrings);
	// ── Metadata ──
	// Kept whole and handed to the JSON reader, then copied again into an attribute if
	// the parse fails — so its length is paid for three times over.
	Result.Metadata = ReadBoundedString(Model->metadata(), MaxMetadataBytes, &RejectedStrings);

	// ── Categories ──
	if (Model->categories())
	{
		// A category is copied into every item that carries one and ends up inside
		// component and asset names, where an over-long string is a fatal FName check
		// rather than an ugly name. The longest standard IFC class name is 45
		// characters, so the cap clears every real one — but note the EndsWith("TYPE")
		// test further down is suffix-sensitive, so lowering it would silently
		// misclassify type objects rather than merely shortening a label.
		// Categories are used strictly parallel to local_ids, so that is the real bound —
		// entries past it can never be indexed, and each one still costs an FString.
		const uint32 ParallelIdCount = Model->local_ids() ? Model->local_ids()->size() : 0u;
		const uint32 CategoryCount = FMath::Min3(Model->categories()->size(), ParallelIdCount, MaxModelItems);
		Result.Categories.Reserve(CategoryCount);
		for (uint32 i = 0; i < CategoryCount; i++)
		{
			Result.Categories.Add(ReadBoundedString(Model->categories()->Get(i), MaxCategoryBytes, &RejectedStrings));
		}
	}

	// ── LocalIds ──
	// local_ids is a parallel array: local_ids[dense_index] = ifc_express_id
	// We build a reverse map so we can convert IFC Express IDs (used in SpatialStructure,
	// guids_items, relations_items and inside relation tuples) back to dense indices
	// (used everywhere else: categories, attributes, instances).
	TArray<uint32> LocalIds;
	TMap<uint32, int32> ExpressIdToDenseIndex;
	if (Model->local_ids())
	{
		uint32 LocalIdCount = Model->local_ids()->size();

		// See MaxModelItems: 4 bytes in the file buys ~232 bytes of FFragItemMetadata
		// once Result.Items is sized from this, plus a map entry here.
		if (LocalIdCount > MaxModelItems)
		{
			UE_LOG(LogFragmentsUE, Error,
				TEXT("Model declares %u items, past the %u this importer supports — truncating. ")
				TEXT("Elements past the cut will not import."),
				LocalIdCount, MaxModelItems);
			LocalIdCount = MaxModelItems;
		}

		LocalIds.Reserve(LocalIdCount);
		ExpressIdToDenseIndex.Reserve(LocalIdCount);

		for (uint32 i = 0; i < LocalIdCount; i++)
		{
			uint32 ExpressId = Model->local_ids()->Get(i);
			LocalIds.Add(ExpressId);
			ExpressIdToDenseIndex.Add(ExpressId, static_cast<int32>(i));
		}
	}

	// ── GUIDs → LocalId map ──
	// guids_items[k] holds the *express id* of the item that owns guids[k]
	// (same convention as relations_items) — NOT a dense index into guids.
	TMap<int32, FString> LocalIdToGuid;
	if (Model->guids() && Model->guids_items())
	{
		const auto* Guids = Model->guids();
		const auto* GuidsItems = Model->guids_items();

		// guids is a vector of strings, which the verifier does not bound the way it
		// bounds a vector of tables — and every offset in it may point at the same
		// string. Both the pair count and the string length therefore have to be
		// capped here: without the length cap one large string is copied once per
		// distinct key, which is the largest amplification in the file.
		const uint32 PairCount = FMath::Min3(Guids->size(), GuidsItems->size(), MaxModelItems);

		for (uint32 k = 0; k < PairCount; k++)
		{
			if (const int32* DenseIdx = ExpressIdToDenseIndex.Find(GuidsItems->Get(k)))
			{
				FString Guid = ReadBoundedString(Guids->Get(k), MaxGuidBytes, &RejectedStrings);
				if (!Guid.IsEmpty())
				{
					LocalIdToGuid.Add(*DenseIdx, MoveTemp(Guid));
				}
			}
		}

		Result.TotalElements = Guids->size();
	}

	if (RejectedStrings > 0)
	{
		UE_LOG(LogFragmentsUE, Warning,
			TEXT("%d string(s) — model guid, header, category or element GlobalId — were declared far longer than that kind of value can be, and were dropped."),
			RejectedStrings);
	}

	// ── IFC metadata (attributes, relations, property sets, materials) ──
	if (Options.bImportMetadata)
	{
		FragMetadata::BuildItemMetadata(Model, LocalIds, ExpressIdToDenseIndex, LocalIdToGuid, Options, BufferSize, Result);

		FFragItemMetadata ModelInfo;
		FragMetadata::BuildModelInfo(Result, ModelInfo);
		Result.ModelInfo = MoveTemp(ModelInfo);
	}

	// ── Meshes (geometry container) ──
	const auto* Meshes = Model->meshes();
	if (!Meshes)
	{
		Result.bSuccess = false;
		Result.ErrorMessage = TEXT("Model has no meshes");
		return Result;
	}

	// ── Model placement ──
	// The coordinates transform is how a BIM model is located geographically.
	if (Options.bImportMetadata && Meshes->coordinates())
	{
		const auto& Origin = Meshes->coordinates()->position();
		Result.ModelInfo.Attributes.Add(FFragAttribute(
			TEXT("Model Origin"),
			FString::Printf(TEXT("%.6f, %.6f, %.6f"), Origin.x(), Origin.y(), Origin.z()),
			TEXT("metres, source coordinate system")));
	}

	// ── Materials ──
	TArray<FLinearColor> MaterialColors;
	TArray<bool> MaterialDoubleSided;
	if (Meshes->materials())
	{
		// Material is a 6-byte struct, so this vector's length is bounded by nothing but
		// the buffer — and the two logs below fire per entry, which makes an uncapped
		// loop a denial of service against the log file before it is one against memory.
		const uint32 MaterialCount = FMath::Min(Meshes->materials()->size(), MaxModelItems);
		if (Meshes->materials()->size() > MaterialCount)
		{
			UE_LOG(LogFragmentsUE, Error, TEXT("Model declares %u materials; reading the first %u."),
				Meshes->materials()->size(), MaterialCount);
		}
		MaterialColors.Reserve(MaterialCount);
		MaterialDoubleSided.Reserve(MaterialCount);

		for (uint32 i = 0; i < MaterialCount; i++)
		{
			const auto* Mat = Meshes->materials()->Get(i);
			// The Fragment schema stores color channels. They might be [0, 1] or [0, 255] depending on exporter version.
			float R = static_cast<float>(Mat->r());
			float G = static_cast<float>(Mat->g());
			float B = static_cast<float>(Mat->b());
			float A = static_cast<float>(Mat->a());
			

			// Auto-detect range: if any value is > 1.0, it's already 0-255. Otherwise, assume 0-1.
			float ScaleVal = (R > 1.0f || G > 1.0f || B > 1.0f || A > 1.0f) ? 255.0f : 1.0f;
			
			// We MUST pass these raw values directly to FLinearColor. 
			// FMeshDescription stores these as linear values. When UStaticMesh builds, it converts them 
			// to 8-bit sRGB, and then the Material's VertexColor node converts them back to Linear.
			// This matches exactly what the Material Instance (BaseColor) was receiving before.
			FLinearColor Color(R / ScaleVal, G / ScaleVal, B / ScaleVal, A / ScaleVal);
			

			MaterialColors.Add(Color);
			MaterialDoubleSided.Add(Mat->rendered_faces() == RenderedFaces::TWO);
		}
	}

	// Each Representation references a geometry (Shell or CircleExtrusion)
	// via its id and representation_class.
	const auto* Representations = Meshes->representations();

	// ── Parse Shells (B-rep geometry) ──
	if (Meshes->shells())
	{
		const auto* Shells = Meshes->shells();
		// ── Budgets and counters for the whole geometry pass ──
		//
		// Every length below is file-controlled, and the FlatBuffers verifier only
		// proves a vector lies inside the buffer — it says nothing about the memory a
		// reader will spend on it. Worse, offsets may alias: any number of profiles can
		// point at one index vector, so cost is not bounded by file size either. Two
		// separate things therefore need bounding, and neither implies the other.
		//
		// Memory: one profile index costs roughly 36 bytes across the arrays built per
		// face and the vertices they produce, so the vertex allowance is what keeps a
		// small file from becoming tens of gigabytes.
		//
		// Time: triangulation is superlinear in ring size, so a vertex allowance cannot
		// bound it — a thousand faces just under any per-face cap still runs for hours.
		// The work allowance is spent inside the solver instead. See earcut.hpp.
		constexpr uint32 MaxShellPoints = 4 * 1000 * 1000;
		constexpr uint32 MaxFaceRingVertices = 8192;
		int64 GeometryVertexBudget = 32 * 1000 * 1000;

		// The point pool is read for every shell, before any profile is triangulated, so
		// the per-shell cap alone bounds nothing: shells is a vector of tables and its
		// offsets may all point at one shell, which is then re-read once per offset.
		int64 ShellPointBudget = 64 * 1000 * 1000;
		int64 EarcutWorkBudget = 2000LL * 1000 * 1000;

		int32 NonFinitePoints = 0;
		int32 DroppedShellPoints = 0;
		int32 OversizedFaces = 0;
		int32 BudgetedOutFaces = 0;
		int32 UntriangulatedFaces = 0;

		for (uint32 ShellIdx = 0; ShellIdx < Shells->size(); ShellIdx++)
		{
			const auto* Shell = Shells->Get(ShellIdx);
			FFragGeometry Geom;
			Geom.GeometryIndex = static_cast<int32>(ShellIdx);

			// Determine if this is a "Big" shell (uint32 indices) or regular (uint16)
			bool bIsBigShell = (Shell->type() == ShellType::BIG);

			// Read the raw vertex pool from the Shell — NO coordinate conversion yet!
			// ThatOpen triangulates in raw (Y-up, meters) space, and so must we.
			TArray<FVector> RawPoints;
			if (Shell->points())
			{
				const auto* Points = Shell->points();
				const uint32 PointCount = (ShellPointBudget > 0)
					? FMath::Min(Points->size(), MaxShellPoints)
					: 0u;
				if (Points->size() > PointCount)
				{
					DroppedShellPoints++;
				}
				ShellPointBudget -= PointCount;
				RawPoints.Reserve(PointCount);
				for (uint32 v = 0; v < PointCount; v++)
				{
					const auto* P = Points->Get(v);
					// Store raw coordinates — NOT converted to UE space
					const FVector RawPos(P->x(), P->y(), P->z());

					// Neutralised here, at the one place every coordinate enters, rather
					// than per face: a single non-finite component poisons everything
					// downstream. The Newell normal goes NaN, so the projection axes are
					// chosen by comparisons that are all false; every earcut area test
					// against it is false, so no ear is ever found and the ring survives
					// into the quadratic search; the crease-merge dot product never
					// matches, so vertex dedup degrades to a full scan per face; and the
					// NaN finally reaches the static-mesh build. Substituting the origin
					// leaves the face wrong but finite and bounded, which is the property
					// everything after this point relies on.
					if (RawPos.ContainsNaN())
					{
						NonFinitePoints++;
						RawPoints.Add(FVector::ZeroVector);
					}
					else
					{
						RawPoints.Add(RawPos);
					}
				}
			}

			// Pre-process holes for this shell. Group them by the profile index they belong to.
			TMap<uint32, TArray<uint32>> HolesByProfile;
			auto ProcessHoles = [&](auto* HolesList)
			{
				if (!HolesList) return;
				for (uint32 h = 0; h < HolesList->size(); h++)
				{
					const auto* Hole = HolesList->Get(h);
					uint32 ProfileId = Hole->profile_id();
					HolesByProfile.FindOrAdd(ProfileId).Add(h);
				}
			};

			if (bIsBigShell)
			{
				ProcessHoles(Shell->big_holes());
			}
			else
			{
				ProcessHoles(Shell->holes());
			}

			// ── Triangulate each profile (polygon face) ──
			// Triangulate in RAW coordinate space (matching ThatOpen exactly),
			// then convert the resulting vertices to UE space.
			auto TriangulateProfiles = [&](auto* ProfilesList, auto* HolesList)
			{
				if (!ProfilesList) return;
				TMap<int32, TArray<int32>> RawIdxToGeomIndices;
				for (uint32 p = 0; p < ProfilesList->size(); p++)
				{
					const auto* Profile = ProfilesList->Get(p);
					if (!Profile->indices() || Profile->indices()->size() < 3)
						continue;

					const uint32 FaceVertexCount = Profile->indices()->size();

					// Both tests come before anything is reserved. The old placement was
					// after the arrays below were already built from this same count, so
					// the check that was meant to bound the face had nothing left to bound
					// — a single profile claiming hundreds of millions of vertices had
					// already asked for tens of gigabytes by the time it was consulted.
					//
					// An oversized face is dropped rather than fan-triangulated. A fan is
					// only correct for a convex ring, it ignores holes entirely — so a
					// perforated face would come out solid — and its own output is as
					// unbounded as the input. A missing face is a smaller and more visible
					// lie than a sealed one.
					if (FaceVertexCount > MaxFaceRingVertices)
					{
						OversizedFaces++;
						continue;
					}

					if (GeometryVertexBudget <= 0)
					{
						BudgetedOutFaces++;
						continue;
					}
					GeometryVertexBudget -= FaceVertexCount;

					// Read the polygon's OUTER vertex positions in RAW coordinates
					TArray<FVector> FacePositions;
					TArray<int32> FaceRawIndices;
					FacePositions.Reserve(FaceVertexCount);
					FaceRawIndices.Reserve(FaceVertexCount);
					for (uint32 vi = 0; vi < FaceVertexCount; vi++)
					{
						int32 RawIdx = static_cast<int32>(Profile->indices()->Get(vi));
						FaceRawIndices.Add(RawIdx);
						if (RawIdx >= 0 && RawIdx < RawPoints.Num())
						{
							FacePositions.Add(RawPoints[RawIdx]);
						}
						else
						{
							FacePositions.Add(FVector::ZeroVector);
						}
					}

					// Compute face normal using Newell method on the OUTER boundary
					FVector FaceNormal(0, 0, 0);
					const int32 NumVerts = FacePositions.Num();
					for (int32 i = 0; i < NumVerts; i++)
					{
						int32 j = (i + 1) % NumVerts;
						const FVector& Pi = FacePositions[i];
						const FVector& Pj = FacePositions[j];
						FaceNormal.X += (Pi.Y - Pj.Y) * (Pi.Z + Pj.Z);
						FaceNormal.Y += (Pi.Z - Pj.Z) * (Pi.X + Pj.X);
						FaceNormal.Z += (Pi.X - Pj.X) * (Pi.Y + Pj.Y);
					}
					FaceNormal = FaceNormal.GetSafeNormal();
					if (FaceNormal.IsNearlyZero())
					{
						FaceNormal = FVector(0, 0, 1);
					}

					// Project to 2D by dropping the axis with the largest normal component.
					// CRITICAL: We must preserve winding order by swapping axes if looking in the negative direction.
					// This exactly matches ThatOpen's FaceUtils.getEarcutDimensions.
					double AbsX = FMath::Abs(FaceNormal.X);
					double AbsY = FMath::Abs(FaceNormal.Y);
					double AbsZ = FMath::Abs(FaceNormal.Z);

					int32 Dim0, Dim1;
					if (AbsZ > AbsX && AbsZ > AbsY)
					{
						if (FaceNormal.Z > 0) {
							Dim0 = 0; Dim1 = 1;
						} else {
							Dim0 = 1; Dim1 = 0;
						}
					}
					else if (AbsY > AbsX && AbsY > AbsZ)
					{
						if (FaceNormal.Y > 0) {
							Dim0 = 2; Dim1 = 0;
						} else {
							Dim0 = 0; Dim1 = 2;
						}
					}
					else
					{
						if (FaceNormal.X > 0) {
							Dim0 = 1; Dim1 = 2;
						} else {
							Dim0 = 2; Dim1 = 1;
						}
					}

					using Point2D = std::array<double, 2>;
					std::vector<std::vector<Point2D>> Polygon; // 0 is outer ring, 1+ are holes
					Polygon.push_back(std::vector<Point2D>());
					Polygon[0].reserve(FacePositions.Num());

					for (const FVector& Pos : FacePositions)
					{
						double Coords[3] = { Pos.X, Pos.Y, Pos.Z };
						Polygon[0].push_back({ Coords[Dim0], Coords[Dim1] });
					}

					// Append hole geometry
					if (TArray<uint32>* HoleIndices = HolesByProfile.Find(p))
					{
						if (HolesList)
						{
							for (uint32 hId : *HoleIndices)
							{
								const auto* Hole = HolesList->Get(hId);
								if (!Hole->indices() || Hole->indices()->size() < 3) continue;

								// Holes are file-controlled the same way the outer ring is,
								// and are appended to the same flat arrays, so they need the
								// same bound. A hole that cannot be represented is skipped
								// rather than silently sealing the face's opening.
								if (Hole->indices()->size() > MaxFaceRingVertices
									|| GeometryVertexBudget <= 0)
								{
									OversizedFaces++;
									continue;
								}
								GeometryVertexBudget -= Hole->indices()->size();

								std::vector<Point2D> HoleRing;
								HoleRing.reserve(Hole->indices()->size());

								for (uint32 vi = 0; vi < Hole->indices()->size(); vi++)
								{
									int32 RawIdx = static_cast<int32>(Hole->indices()->Get(vi));
									FaceRawIndices.Add(RawIdx);
									FVector HolePos = FVector::ZeroVector;
									if (RawIdx >= 0 && RawIdx < RawPoints.Num())
									{
										HolePos = RawPoints[RawIdx];
									}
									
									FacePositions.Add(HolePos); // Append to flat array so indices match
									
									double Coords[3] = { HolePos.X, HolePos.Y, HolePos.Z };
									HoleRing.push_back({ Coords[Dim0], Coords[Dim1] });
								}
								Polygon.push_back(HoleRing);
							}
						}
					}

					// The solver draws on the model-wide work allowance. Ring size alone
					// cannot bound its cost: splitEarcut is a nested search whose validity
					// test walks the whole ring, so a face can be O(n^3) while sitting well
					// under any per-face vertex cap, and the file also controls how many
					// such faces there are. Charging the actual inner-loop steps is the only
					// bound that holds, and it costs a well-behaved polygon nothing — those
					// clip their ears and never reach the search at all.
					bool bBudgetExhausted = false;
					std::vector<uint32_t> TriIndices =
						mapbox::earcut<uint32_t>(Polygon, &EarcutWorkBudget, bBudgetExhausted);

					if (bBudgetExhausted)
					{
						UntriangulatedFaces++;
					}

					// A ring within the size cap can still defeat the solver — a genuinely
					// degenerate profile returns nothing. The fan is correct only for a
					// convex ring and ignores holes, but at this size it is bounded, and
					// some geometry beats none.
					if (TriIndices.empty() && FaceVertexCount >= 3)
					{
						// Fallback: simple fan triangulation
						for (uint32 vi = 1; vi + 1 < FaceVertexCount; vi++)
						{
							TriIndices.push_back(0);
							TriIndices.push_back(vi);
							TriIndices.push_back(vi + 1);
						}
					}

					// Now convert face positions from raw to UE coordinates and add to geometry
					// Convert normal from raw Y-up to UE Z-up
					FVector UENormal = ConvertPosition(
						static_cast<float>(FaceNormal.X),
						static_cast<float>(FaceNormal.Y),
						static_cast<float>(FaceNormal.Z), 1.0f);
					// Negate to match the reversed triangle winding (lines below swap idx 1 & 2)
					UENormal = -UENormal;

					TArray<int32> FaceGeomIndices;
					FaceGeomIndices.Reserve(FacePositions.Num());

					for (int32 FaceVi = 0; FaceVi < FacePositions.Num(); FaceVi++)
					{
						int32 RawIdx = FaceRawIndices[FaceVi];
						const FVector& RawPos = FacePositions[FaceVi];
						int32 GeomIdx = -1;

						if (TArray<int32>* FoundIndices = RawIdxToGeomIndices.Find(RawIdx))
						{
							for (int32 ExistingGeomIdx : *FoundIndices)
							{
								FVector ExistingNormal = Geom.Normals[ExistingGeomIdx].GetSafeNormal();
								if (FVector::DotProduct(ExistingNormal, UENormal) >= 0.7f) // ~45 degrees crease angle
								{
									GeomIdx = ExistingGeomIdx;
									break;
								}
							}
						}

						if (GeomIdx != -1)
						{
							Geom.Normals[GeomIdx] += UENormal;
						}
						else
						{
							FVector UEPos = ConvertPosition(
								static_cast<float>(RawPos.X),
								static_cast<float>(RawPos.Y),
								static_cast<float>(RawPos.Z), Scale);
							GeomIdx = Geom.Positions.Add(UEPos);
							Geom.Normals.Add(UENormal);
							RawIdxToGeomIndices.FindOrAdd(RawIdx).Add(GeomIdx);
						}
						FaceGeomIndices.Add(GeomIdx);
					}

					// The reflection in ConvertPosition changes the coordinate handedness.
					// We MUST reverse the triangle winding order so they face outward in UE.
					for (size_t i = 0; i < TriIndices.size(); i += 3)
					{
						Geom.Indices.Add(FaceGeomIndices[static_cast<int32>(TriIndices[i + 0])]);
						Geom.Indices.Add(FaceGeomIndices[static_cast<int32>(TriIndices[i + 2])]);
						Geom.Indices.Add(FaceGeomIndices[static_cast<int32>(TriIndices[i + 1])]);
					}
				}

			};

			if (bIsBigShell)
			{
				TriangulateProfiles(Shell->big_profiles(), Shell->big_holes());
			}
			else
			{
				TriangulateProfiles(Shell->profiles(), Shell->holes());
			}

			// ── Finalize geometry ──
			if (Geom.Positions.Num() > 0 && Geom.Indices.Num() >= 3)
			{
				for (FVector& N : Geom.Normals)
				{
					N.Normalize();
				}
				// Bounding box
				Geom.BoundingBox = FBox(ForceInit);
				for (const FVector& Pos : Geom.Positions)
				{
					Geom.BoundingBox += Pos;
				}

				Result.TotalVertices += Geom.Positions.Num();
				Result.TotalTriangles += Geom.Indices.Num() / 3;
			}

			Result.Geometries.Add(MoveTemp(Geom));
		}

		// Reported once for the model, not once per shell: a crafted file can hold
		// millions of shells, and a warning per shell is its own denial of service
		// against the log.
		if (NonFinitePoints > 0)
		{
			UE_LOG(LogFragmentsUE, Warning,
				TEXT("%d vertex position(s) were not finite and were moved to the origin; that geometry is wrong."),
				NonFinitePoints);
		}
		if (DroppedShellPoints > 0)
		{
			UE_LOG(LogFragmentsUE, Warning,
				TEXT("%d shell(s) declared more than %u points; the remainder were dropped."),
				DroppedShellPoints, MaxShellPoints);
		}
		if (OversizedFaces > 0)
		{
			UE_LOG(LogFragmentsUE, Warning,
				TEXT("Dropped %d face(s)/hole(s) past the %u-vertex limit for a single ring."),
				OversizedFaces, MaxFaceRingVertices);
		}
		if (BudgetedOutFaces > 0)
		{
			UE_LOG(LogFragmentsUE, Error,
				TEXT("Geometry allowance spent: %d face(s) were not built. The model is larger than this importer will load in one pass."),
				BudgetedOutFaces);
		}
		if (UntriangulatedFaces > 0)
		{
			UE_LOG(LogFragmentsUE, Error,
				TEXT("Triangulation allowance spent on %d face(s); they fell back to a fan or were left empty. ")
				TEXT("This usually means self-intersecting or degenerate profiles."),
				UntriangulatedFaces);
		}
	}

	// ── Parse Samples (instances) ──
	if (Meshes->samples() && Meshes->meshes_items())
	{
		const auto* Samples = Meshes->samples();
		const auto* MeshesItems = Meshes->meshes_items();
		const auto* LocalTransforms = Meshes->local_transforms();
		const auto* GlobalTransforms = Meshes->global_transforms();

		int32 SkippedSamples = 0;

		// Sample is a 16-byte struct against ~192 bytes of FFragInstance, and the vector
		// grows by doubling with no Reserve — so an uncapped loop is a fatal allocation
		// failure, which ends the process rather than the import.
		const uint32 SampleCount = FMath::Min(Samples->size(), MaxModelItems);
		if (Samples->size() > SampleCount)
		{
			UE_LOG(LogFragmentsUE, Error,
				TEXT("Model declares %u samples; building the first %u. The rest of the geometry will be missing."),
				Samples->size(), SampleCount);
		}
		Result.Instances.Reserve(SampleCount);

		for (uint32 SampleIdx = 0; SampleIdx < SampleCount; SampleIdx++)
		{
			const auto* SampleData = Samples->Get(SampleIdx);
			FFragInstance Instance;

			// Sample.item → index into meshes_items → localId.
			// Samples that resolve to no local id are dropped, not kept with LocalId -1:
			// spatial bucket nodes carry that same id, so the geometry would be re-attached
			// under every bucket in the tree. Their global transform is missing too — it is
			// indexed by the same item index.
			uint32 ItemIndex = SampleData->item();
			if (ItemIndex >= MeshesItems->size())
			{
				SkippedSamples++;
				continue;
			}

			// meshes_items entries index local_ids; a 2 GB FlatBuffer cannot hold 2^31 of
			// them, so a larger value is corrupt and would alias the -1 sentinel here.
			const uint32 RawLocalId = MeshesItems->Get(ItemIndex);
			if (RawLocalId > static_cast<uint32>(MAX_int32))
			{
				SkippedSamples++;
				continue;
			}

			Instance.LocalId = static_cast<int32>(RawLocalId);

			// Sample.representation → index into Representations → geometry
			uint32 RepIdx = SampleData->representation();
			if (Representations && RepIdx < Representations->size())
			{
				const auto* Rep = Representations->Get(RepIdx);
				if (Rep->representation_class() == RepresentationClass::SHELL)
				{
					Instance.GeometryIndex = static_cast<int32>(Rep->id());
				}
				// TODO: Handle CircleExtrusion representations
			}

			// Sample.material → index into Materials
			Instance.MaterialIndex = static_cast<int32>(SampleData->material());
			if (Instance.MaterialIndex >= 0 && Instance.MaterialIndex < MaterialColors.Num())
			{
				Instance.Color = MaterialColors[Instance.MaterialIndex];
				Instance.Opacity = MaterialColors[Instance.MaterialIndex].A;
				Instance.bDoubleSided = MaterialDoubleSided[Instance.MaterialIndex];
			}

			// Build transform from local + global transforms
			FTransform LocalTransform = FTransform::Identity;
			FTransform GlobalTransform = FTransform::Identity;

			// Local transform
			uint32 LocalTransformIdx = SampleData->local_transform();
			if (LocalTransforms && LocalTransformIdx < LocalTransforms->size())
			{
				const auto* LT = LocalTransforms->Get(LocalTransformIdx);
				LocalTransform = BuildTransform(
					LT->position().x(), LT->position().y(), LT->position().z(),
					LT->x_direction().x(), LT->x_direction().y(), LT->x_direction().z(),
					LT->y_direction().x(), LT->y_direction().y(), LT->y_direction().z(),
					Scale);
			}

			// Global transform (world positioning)
			if (GlobalTransforms && ItemIndex < GlobalTransforms->size())
			{
				const auto* GT = GlobalTransforms->Get(ItemIndex);
				GlobalTransform = BuildTransform(
					GT->position().x(), GT->position().y(), GT->position().z(),
					GT->x_direction().x(), GT->x_direction().y(), GT->x_direction().z(),
					GT->y_direction().x(), GT->y_direction().y(), GT->y_direction().z(),
					Scale);
			}

			// Combined transform: Global * Local
			Instance.Transform = LocalTransform * GlobalTransform;

			// Look up GUID
			if (const FString* FoundGuid = LocalIdToGuid.Find(Instance.LocalId))
			{
				Instance.GUID = *FoundGuid;
			}

			Result.Instances.Add(MoveTemp(Instance));
		}

		Result.TotalInstances = Result.Instances.Num();
		if (SkippedSamples > 0)
		{
			UE_LOG(LogFragmentsUE, Warning, TEXT("Skipped %d sample(s): item index does not resolve to a local id"), SkippedSamples);
		}
	}

	// Helper to extract Name from JSON string attributes.
	// Uses the parsed metadata when available and falls back to a direct scan of
	// the attribute strings when metadata import is disabled.
	// This runs once per spatial node and once per instance — both counts set by the
	// file — and each call scans one item's whole attribute list. Three separate things
	// are needed to bound it, and any two of them still leave it open:
	//   the cache, so one item's list is not re-scanned per node that names it;
	//   the length cap, because the tuple sought is ["Name","<IfcLabel>","IFCLABEL"]
	//     and nothing here needs the 4 MiB a general tuple is allowed;
	//   the budget, because distinct ids still multiply the first two together.
	TMap<int32, FString> ExtractedNameCache;
	int64 NameScanByteBudget = 64LL * 1024 * 1024;
	constexpr uint32 MaxNameTupleBytes = 4096;

	auto ExtractNameFromAttributes = [&](int32 LocalId) -> FString
	{
		if (const FFragItemMetadata* Item = Result.FindItem(LocalId))
		{
			return Item->Name;
		}

		if (const FString* Cached = ExtractedNameCache.Find(LocalId))
		{
			return *Cached;
		}

		if (LocalId >= 0 && Model->attributes() && static_cast<uint32>(LocalId) < Model->attributes()->size()
			&& NameScanByteBudget > 0)
		{
			const Attribute* Attr = Model->attributes()->Get(LocalId);
			if (Attr && Attr->data())
			{
				const uint32 NameScanCount = FMath::Min(Attr->data()->size(), MaxItemAttributes);
				for (uint32 j = 0; j < NameScanCount; j++)
				{
					// Charged at the string's real length, and only once it is known to be
					// worth converting. Charging the cap instead would bill a 60-byte name
					// tuple as 4 KB and starve a legitimate model of its own names; charging
					// rejected strings would let one oversized entry drain the allowance
					// for everything after it.
					const flatbuffers::String* NameTuple = Attr->data()->Get(j);
					if (!NameTuple || NameTuple->size() > MaxNameTupleBytes)
					{
						continue;
					}

					NameScanByteBudget -= NameTuple->size();
					if (NameScanByteBudget <= 0)
					{
						break;
					}

					FString AttrStr = FString(UTF8_TO_TCHAR(NameTuple->c_str()));
					if (AttrStr.StartsWith(TEXT("[\"Name\",")))
					{
						int32 FirstQuote = AttrStr.Find(TEXT("\""), ESearchCase::IgnoreCase, ESearchDir::FromStart, 8);
						if (FirstQuote != INDEX_NONE)
						{
							int32 SearchStart = FirstQuote + 1;
							while (SearchStart < AttrStr.Len())
							{
								int32 SecondQuote = AttrStr.Find(TEXT("\""), ESearchCase::IgnoreCase, ESearchDir::FromStart, SearchStart);
								if (SecondQuote == INDEX_NONE) break;
								
								// Count preceding backslashes to see if this quote is escaped
								int32 BackslashCount = 0;
								for (int32 k = SecondQuote - 1; k >= 0; --k)
								{
									if (AttrStr[k] == TEXT('\\')) BackslashCount++;
									else break;
								}
								
								if (BackslashCount % 2 == 0) 
								{
									// Not escaped, this is the true end quote
									FString Extracted = AttrStr.Mid(FirstQuote + 1, SecondQuote - FirstQuote - 1);
									// Clean up JSON escapes
									Extracted = Extracted.Replace(TEXT("\\\""), TEXT("\""));
									Extracted = Extracted.Replace(TEXT("\\\\"), TEXT("\\"));
									return ExtractedNameCache.Add(LocalId, MoveTemp(Extracted));
								}
								SearchStart = SecondQuote + 1;
							}
						}
					}
				}
			}
		}

		// Cached even when nothing was found, so a miss costs one scan rather than one
		// per node that names this id.
		return ExtractedNameCache.Add(LocalId, FString());
	};

	// ── Parse Spatial Structure ──
	if (Model->spatial_structure())
	{
		TFunction<void(const SpatialStructure*, FFragSpatialNode&)> ParseSpatialNode =
			[&](const SpatialStructure* Node, FFragSpatialNode& OutNode)
		{
			// SpatialStructure.local_id is an IFC Express ID (e.g. 321772).
			// Convert it to the dense index used by categories[], attributes[], and instances.
			if (Node->local_id().has_value())
			{
				uint32 ExpressId = Node->local_id().value();
				OutNode.ExpressId = ExpressId;
				if (const int32* DenseIdx = ExpressIdToDenseIndex.Find(ExpressId))
				{
					OutNode.LocalId = *DenseIdx;
				}
				else
				{
					OutNode.LocalId = -1; // Express ID not found in local_ids array
				}
			}
			else
			{
				OutNode.LocalId = -1;
			}
			if (Node->category())
			{
				// Reaches SetActorLabel by way of NodeLabel, and is stored on every node
				// of a tree whose size the file controls.
				OutNode.Category = ReadBoundedString(Node->category(), MaxCategoryBytes);
			}
			OutNode.Name = ExtractNameFromAttributes(OutNode.LocalId);
			if (Node->children())
			{
				for (uint32 c = 0; c < Node->children()->size(); c++)
				{
					FFragSpatialNode ChildNode;
					ParseSpatialNode(Node->children()->Get(c), ChildNode);
					OutNode.Children.Add(MoveTemp(ChildNode));
				}
			}
		};

		ParseSpatialNode(Model->spatial_structure(), Result.SpatialRoot);
	}

	// ── Apply BIM identity to instances ──
	// Categories are parallel to local ids; names come from the item attributes.
	// Runs whether or not the file carries a spatial structure.
	for (FFragInstance& Inst : Result.Instances)
	{
		if (const FFragItemMetadata* Item = Result.FindItem(Inst.LocalId))
		{
			Inst.Category = Item->Category;
			Inst.Name = Item->Name;
			if (!Item->GUID.IsEmpty())
			{
				Inst.GUID = Item->GUID;
			}
			continue;
		}

		if (Inst.LocalId >= 0 && Inst.LocalId < Result.Categories.Num())
		{
			Inst.Category = Result.Categories[Inst.LocalId];
		}
		Inst.Name = ExtractNameFromAttributes(Inst.LocalId);
	}


	// ── Summary ──
	Result.bSuccess = true;
	return Result;
}

// ─────────────────────────────────────────────────────────────────────────────
// Coordinate Conversion
// ─────────────────────────────────────────────────────────────────────────────

FVector FFragParser::ConvertPosition(float X, float Y, float Z, float Scale)
{
	// Fragments (RH Y-up, meters) → UE (LH Z-up, cm)
	// We apply a reflection matrix C to change handedness:
	// UE X = Forward = -RH Z
	// UE Y = Right   = RH X
	// UE Z = Up      = RH Y
	return FVector(
		-Z * Scale,
		 X * Scale,
		 Y * Scale
	);
}

FVector FFragParser::ConvertPositionD(double X, double Y, double Z, float Scale)
{
	return FVector(
		-Z * static_cast<double>(Scale),
		 X * static_cast<double>(Scale),
		 Y * static_cast<double>(Scale)
	);
}

FTransform FFragParser::BuildTransform(
	double PosX, double PosY, double PosZ,
	float XDirX, float XDirY, float XDirZ,
	float YDirX, float YDirY, float YDirZ,
	float Scale)
{
	// Convert position
	FVector Position = ConvertPositionD(PosX, PosY, PosZ, Scale);

	// Fragments RH vectors
	FVector FragX(XDirX, XDirY, XDirZ);
	FVector FragY(YDirX, YDirY, YDirZ);
	FVector FragZ = FVector::CrossProduct(FragX, FragY);

	// Using M_ue = C * M_rh * C^T where C maps (X,Y,Z) to (-Z,X,Y).
	// This guarantees a valid Left-Handed rotation matrix with det=1.
	// Col 0 (UE Forward) = C * (-FragZ) = -ConvertPosition(FragZ)
	// Col 1 (UE Right)   = C * (FragX)  = ConvertPosition(FragX)
	// Col 2 (UE Up)      = C * (FragY)  = ConvertPosition(FragY)
	FVector UE_Row0 = -ConvertPosition(FragZ.X, FragZ.Y, FragZ.Z, 1.0f).GetSafeNormal();
	FVector UE_Row1 =  ConvertPosition(FragX.X, FragX.Y, FragX.Z, 1.0f).GetSafeNormal();
	FVector UE_Row2 =  ConvertPosition(FragY.X, FragY.Y, FragY.Z, 1.0f).GetSafeNormal();

	FMatrix RotMatrix(UE_Row0, UE_Row1, UE_Row2, FVector::ZeroVector);

	FQuat Rotation = RotMatrix.ToQuat();
	Rotation.Normalize();

	return FTransform(Rotation, Position, FVector::OneVector);
}

// ─────────────────────────────────────────────────────────────────────────────
// Normal Computation
// ─────────────────────────────────────────────────────────────────────────────

void FFragParser::ComputeFlatNormals(FFragGeometry& Geometry)
{
	const int32 VertexCount = Geometry.Positions.Num();
	Geometry.Normals.SetNumZeroed(VertexCount);

	// Accumulate face normals onto each vertex
	for (int32 i = 0; i + 2 < Geometry.Indices.Num(); i += 3)
	{
		int32 I0 = Geometry.Indices[i];
		int32 I1 = Geometry.Indices[i + 1];
		int32 I2 = Geometry.Indices[i + 2];

		// The lower bound is the more dangerous of the two here: these indices are used
		// to write into Normals, not only to read, so a negative one is an out-of-bounds
		// store rather than a garbage read.
		if (I0 < 0 || I1 < 0 || I2 < 0
			|| I0 >= VertexCount || I1 >= VertexCount || I2 >= VertexCount)
			continue;

		const FVector& V0 = Geometry.Positions[I0];
		const FVector& V1 = Geometry.Positions[I1];
		const FVector& V2 = Geometry.Positions[I2];

		FVector Edge1 = V1 - V0;
		FVector Edge2 = V2 - V0;
		FVector FaceNormal = FVector::CrossProduct(Edge1, Edge2);

		// Don't normalize — area-weighted accumulation gives better results for smooth surfaces
		Geometry.Normals[I0] += FaceNormal;
		Geometry.Normals[I1] += FaceNormal;
		Geometry.Normals[I2] += FaceNormal;
	}

	// Normalize accumulated normals
	for (FVector& Normal : Geometry.Normals)
	{
		if (!Normal.IsNearlyZero())
		{
			Normal.Normalize();
		}
		else
		{
			Normal = FVector::UpVector;
		}
	}
}
