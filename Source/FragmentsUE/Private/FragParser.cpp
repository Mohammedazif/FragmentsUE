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

// ─────────────────────────────────────────────────────────────────────────────
// Public API
// ─────────────────────────────────────────────────────────────────────────────

FFragImportResult FFragParser::LoadFromFile(const FString& FilePath, const FFragImportOptions& Options)
{
	UE_LOG(LogFragmentsUE, Log, TEXT("Loading .frag file: %s"), *FilePath);

	TArray<uint8> RawData;
	if (!FFileHelper::LoadFileToArray(RawData, *FilePath))
	{
		FFragImportResult Result;
		Result.bSuccess = false;
		Result.ErrorMessage = FString::Printf(TEXT("Failed to read file: %s"), *FilePath);
		UE_LOG(LogFragmentsUE, Error, TEXT("%s"), *Result.ErrorMessage);
		return Result;
	}

	UE_LOG(LogFragmentsUE, Log, TEXT("Loaded %d bytes from disk"), RawData.Num());

	FFragImportResult Result = LoadFromBuffer(RawData, Options);

	// Derive a clean model name from the file path (strip directory and extension)
	// e.g. "D:/Models/Joyson Model.frag" → "Joyson_Model"
	FString BaseName = FPaths::GetBaseFilename(FilePath);
	BaseName = BaseName.Replace(TEXT(" "), TEXT("_"));
	Result.ModelName = BaseName;

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
		UE_LOG(LogFragmentsUE, Log, TEXT("Decompressed: %d → %d bytes (%.1fx)"),
			RawData.Num(), BufferSize, (float)BufferSize / RawData.Num());
	}
	else
	{
		Buffer = RawData.GetData();
		BufferSize = RawData.Num();
		UE_LOG(LogFragmentsUE, Log, TEXT("Not compressed, using raw buffer (%d bytes)"), BufferSize);
	}

	// Dump first 32 bytes for debugging
	if (BufferSize >= 32)
	{
		FString HexDump;
		for (int32 i = 0; i < 32; i++)
		{
			HexDump += FString::Printf(TEXT("%02X "), Buffer[i]);
		}
		UE_LOG(LogFragmentsUE, Log, TEXT("First 32 bytes: %s"), *HexDump);
	}

	// Verify FlatBuffers integrity
	// NOTE: ThatOpen's JS library serializes with builder.finish() WITHOUT a file identifier,
	// so we must NOT check for the "0001" identifier. Use VerifyBuffer<Model>(nullptr) instead.
	flatbuffers::Verifier Verifier(Buffer, static_cast<size_t>(BufferSize));
	if (!Verifier.VerifyBuffer<Model>(nullptr))
	{
		FFragImportResult Result;
		Result.bSuccess = false;
		Result.ErrorMessage = TEXT("FlatBuffers verification failed — invalid or corrupt .frag data");
		UE_LOG(LogFragmentsUE, Error, TEXT("%s"), *Result.ErrorMessage);
		return Result;
	}

	UE_LOG(LogFragmentsUE, Log, TEXT("FlatBuffers verification passed"));
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

	// zlib magic byte check: first byte 0x78
	// Second byte: 0x01 (no compression), 0x9C (default), 0xDA (best)
	if (Data[0] != 0x78)
		return false;

	if (Data[1] != 0x01 && Data[1] != 0x9C && Data[1] != 0xDA)
		return false;

	UE_LOG(LogFragmentsUE, Log, TEXT("Detected zlib header (0x%02X 0x%02X), attempting decompress..."),
		Data[0], Data[1]);

	z_stream Stream = {};
	Stream.next_in = (Bytef*)Data.GetData();
	Stream.avail_in = Data.Num();

	if (inflateInit(&Stream) != Z_OK)
	{
		UE_LOG(LogFragmentsUE, Error, TEXT("zlib inflateInit failed"));
		return false;
	}

	// Inflate in chunks
	const int32 ChunkSize = 1024 * 1024; // 1 MB chunks
	Out.Empty(Data.Num() * 4);
	
	TArray<uint8> ChunkBuffer;
	ChunkBuffer.SetNumUninitialized(ChunkSize);

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
			Out.Append(ChunkBuffer.GetData(), BytesInflated);
		}
	} while (Stream.avail_out == 0);

	inflateEnd(&Stream);

	UE_LOG(LogFragmentsUE, Log, TEXT("zlib inflate succeeded: %d bytes decompressed"), Out.Num());
	return true;
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

	// ── Model GUID ──
	if (Model->guid())
	{
		Result.ModelGuid = UTF8_TO_TCHAR(Model->guid()->c_str());
	}
	UE_LOG(LogFragmentsUE, Log, TEXT("Model GUID: %s"), *Result.ModelGuid);

	// ── Metadata ──
	if (Model->metadata())
	{
		Result.Metadata = UTF8_TO_TCHAR(Model->metadata()->c_str());
	}

	// ── Categories ──
	if (Model->categories())
	{
		for (uint32 i = 0; i < Model->categories()->size(); i++)
		{
			Result.Categories.Add(UTF8_TO_TCHAR(Model->categories()->Get(i)->c_str()));
		}
		UE_LOG(LogFragmentsUE, Log, TEXT("Categories: %d"), Result.Categories.Num());
	}

	// ── GUIDs → LocalId map ──
	TMap<int32, FString> LocalIdToGuid;
	if (Model->guids() && Model->guids_items())
	{
		const auto* Guids = Model->guids();
		const auto* GuidsItems = Model->guids_items();

		for (uint32 i = 0; i < GuidsItems->size(); i++)
		{
			int32 LocalId = static_cast<int32>(i);
			uint32 GuidIndex = GuidsItems->Get(i);
			if (GuidIndex < Guids->size())
			{
				FString GuidStr = UTF8_TO_TCHAR(Guids->Get(GuidIndex)->c_str());
				LocalIdToGuid.Add(LocalId, GuidStr);
			}
		}

		Result.TotalElements = Guids->size();
		UE_LOG(LogFragmentsUE, Log, TEXT("GUIDs: %d unique, %d mapped to localIds"),
			Guids->size(), LocalIdToGuid.Num());
	}

	// ── LocalIds ──
	// local_ids is a parallel array: local_ids[dense_index] = ifc_express_id
	// We build a reverse map so we can convert IFC Express IDs (used in SpatialStructure)
	// back to dense indices (used everywhere else: categories, attributes, instances).
	TArray<uint32> LocalIds;
	TMap<uint32, int32> ExpressIdToDenseIndex;
	if (Model->local_ids())
	{
		for (uint32 i = 0; i < Model->local_ids()->size(); i++)
		{
			uint32 ExpressId = Model->local_ids()->Get(i);
			LocalIds.Add(ExpressId);
			ExpressIdToDenseIndex.Add(ExpressId, static_cast<int32>(i));
		}
		UE_LOG(LogFragmentsUE, Log, TEXT("LocalIds: %d (reverse map: %d entries)"), LocalIds.Num(), ExpressIdToDenseIndex.Num());

		for (int32 i = 0; i < FMath::Min(10, LocalIds.Num()); i++)
		{
			UE_LOG(LogFragmentsUE, Log, TEXT("  LocalIds[%d] = %d"), i, LocalIds[i]);
		}
	}

	// ── Meshes (geometry container) ──
	const auto* Meshes = Model->meshes();
	if (!Meshes)
	{
		Result.bSuccess = false;
		Result.ErrorMessage = TEXT("Model has no meshes");
		return Result;
	}

	// ── Materials ──
	TArray<FLinearColor> MaterialColors;
	TArray<bool> MaterialDoubleSided;
	if (Meshes->materials())
	{
		for (uint32 i = 0; i < Meshes->materials()->size(); i++)
		{
			const auto* Mat = Meshes->materials()->Get(i);
			// The Fragment schema stores color channels. They might be [0, 1] or [0, 255] depending on exporter version.
			float R = static_cast<float>(Mat->r());
			float G = static_cast<float>(Mat->g());
			float B = static_cast<float>(Mat->b());
			float A = static_cast<float>(Mat->a());
			
			UE_LOG(LogFragmentsUE, Log, TEXT("  Material[%d] raw: R=%.4f G=%.4f B=%.4f A=%.4f"), i, R, G, B, A);
			
			// Auto-detect range: if any value is > 1.0, it's already 0-255. Otherwise, assume 0-1.
			float ScaleVal = (R > 1.0f || G > 1.0f || B > 1.0f || A > 1.0f) ? 1.0f : 255.0f;
			
			// Convert to [0, 255] for FColor, which performs sRGB -> Linear conversion
			FColor SRGBColor(
				FMath::Clamp(static_cast<int32>(R * ScaleVal), 0, 255),
				FMath::Clamp(static_cast<int32>(G * ScaleVal), 0, 255),
				FMath::Clamp(static_cast<int32>(B * ScaleVal), 0, 255),
				FMath::Clamp(static_cast<int32>(A * ScaleVal), 0, 255)
			);
			FLinearColor Color = FLinearColor(SRGBColor);
			
			UE_LOG(LogFragmentsUE, Log, TEXT("  Material[%d] final: R=%.4f G=%.4f B=%.4f A=%.4f (opacity=%.4f)"), 
				i, Color.R, Color.G, Color.B, Color.A, Color.A);
			
			MaterialColors.Add(Color);
			MaterialDoubleSided.Add(Mat->rendered_faces() == RenderedFaces::TWO);
		}
		UE_LOG(LogFragmentsUE, Log, TEXT("Materials: %d"), MaterialColors.Num());
	}

	// ── Representations ──
	// Each Representation references a geometry (Shell or CircleExtrusion)
	// via its id and representation_class.
	const auto* Representations = Meshes->representations();
	if (Representations)
	{
		UE_LOG(LogFragmentsUE, Log, TEXT("Representations: %d"), Representations->size());
	}

	// ── Parse Shells (B-rep geometry) ──
	if (Meshes->shells())
	{
		const auto* Shells = Meshes->shells();
		UE_LOG(LogFragmentsUE, Log, TEXT("Shells: %d"), Shells->size());

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
				RawPoints.Reserve(Points->size());
				for (uint32 v = 0; v < Points->size(); v++)
				{
					const auto* P = Points->Get(v);
					// Store raw coordinates — NOT converted to UE space
					RawPoints.Add(FVector(P->x(), P->y(), P->z()));
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

					std::vector<uint32_t> TriIndices = mapbox::earcut<uint32_t>(Polygon);

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
	}

	// ── Parse Samples (instances) ──
	if (Meshes->samples() && Meshes->meshes_items())
	{
		const auto* Samples = Meshes->samples();
		const auto* MeshesItems = Meshes->meshes_items();
		const auto* LocalTransforms = Meshes->local_transforms();
		const auto* GlobalTransforms = Meshes->global_transforms();

		UE_LOG(LogFragmentsUE, Log, TEXT("Samples: %d, MeshesItems: %d"),
			Samples->size(), MeshesItems->size());

		for (uint32 SampleIdx = 0; SampleIdx < Samples->size(); SampleIdx++)
		{
			const auto* SampleData = Samples->Get(SampleIdx);
			FFragInstance Instance;

			// Sample.item → index into meshes_items → localId
			uint32 ItemIndex = SampleData->item();
			if (ItemIndex < MeshesItems->size())
			{
				Instance.LocalId = static_cast<int32>(MeshesItems->Get(ItemIndex));
			}

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

			// Debug parallel arrays
			if (Instance.LocalId < Result.Categories.Num())
			{
				FString DirectCat = Result.Categories[Instance.LocalId];
				if (DirectCat.Contains(TEXT("WINDOW")))
				{
					UE_LOG(LogFragmentsUE, Log, TEXT("  Instance[%d]: DenseLocalId=%d has DirectCat='%s'"), SampleIdx, Instance.LocalId, *DirectCat);
				}
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
		UE_LOG(LogFragmentsUE, Log, TEXT("Instances parsed: %d"), Result.TotalInstances);
	}

	// Helper to extract Name from JSON string attributes
	auto ExtractNameFromAttributes = [&](int32 LocalId) -> FString
	{
		if (LocalId >= 0 && Model->attributes() && static_cast<uint32>(LocalId) < Model->attributes()->size())
		{
			const Attribute* Attr = Model->attributes()->Get(LocalId);
			if (Attr && Attr->data())
			{
				for (uint32 j = 0; j < Attr->data()->size(); j++)
				{
					FString AttrStr = UTF8_TO_TCHAR(Attr->data()->Get(j)->c_str());
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
									return Extracted;
								}
								SearchStart = SecondQuote + 1;
							}
						}
					}
				}
			}
		}
		return TEXT("");
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
				OutNode.Category = UTF8_TO_TCHAR(Node->category()->c_str());
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
		UE_LOG(LogFragmentsUE, Log, TEXT("Spatial structure parsed (root has %d children)"),
			Result.SpatialRoot.Children.Num());
			
		// Apply Category to Instances using the parallel Categories array
		for (FFragInstance& Inst : Result.Instances)
		{
			if (Inst.LocalId >= 0 && Inst.LocalId < Result.Categories.Num())
			{
				Inst.Category = Result.Categories[Inst.LocalId];
			}
			Inst.Name = ExtractNameFromAttributes(Inst.LocalId);
		}
	}

	// ── Relations ──
	if (Model->relations())
	{
		UE_LOG(LogFragmentsUE, Log, TEXT("Relations: %d"), Model->relations()->size());
	}
	if (Model->relation_names())
	{
		for (uint32 i = 0; i < Model->relation_names()->size(); i++)
		{
			UE_LOG(LogFragmentsUE, Log, TEXT("  Relation name [%d]: %s"), i,
				UTF8_TO_TCHAR(Model->relation_names()->Get(i)->c_str()));
		}
	}

	// ── Attributes ──
	if (Model->attributes())
	{
		UE_LOG(LogFragmentsUE, Log, TEXT("Attributes length: %d"), Model->attributes()->size());
		
		// Debug print first 10 attributes
		for (uint32 i = 0; i < FMath::Min((uint32)10, Model->attributes()->size()); i++)
		{
			const Attribute* Attr = Model->attributes()->Get(i);
			if (Attr && Attr->data())
			{
				FString AttrStr;
				for (uint32 j = 0; j < Attr->data()->size(); j++)
				{
					AttrStr += UTF8_TO_TCHAR(Attr->data()->Get(j)->c_str());
					AttrStr += TEXT(" | ");
				}
				UE_LOG(LogFragmentsUE, Log, TEXT("  Attr[%d]: %s"), i, *AttrStr);
			}
		}
	}
	if (Model->unique_attributes())
	{
		UE_LOG(LogFragmentsUE, Log, TEXT("Unique attribute keys: %d"), Model->unique_attributes()->size());
	}

	// ── Summary ──
	Result.bSuccess = true;
	UE_LOG(LogFragmentsUE, Log, TEXT("═══════════════════════════════════════════════════"));
	UE_LOG(LogFragmentsUE, Log, TEXT("  FragParser: Parse complete"));
	UE_LOG(LogFragmentsUE, Log, TEXT("  Model GUID:    %s"), *Result.ModelGuid);
	UE_LOG(LogFragmentsUE, Log, TEXT("  Geometries:    %d"), Result.Geometries.Num());
	UE_LOG(LogFragmentsUE, Log, TEXT("  Total vertices:  %d"), Result.TotalVertices);
	UE_LOG(LogFragmentsUE, Log, TEXT("  Total triangles: %d"), Result.TotalTriangles);
	UE_LOG(LogFragmentsUE, Log, TEXT("  Instances:     %d"), Result.TotalInstances);
	UE_LOG(LogFragmentsUE, Log, TEXT("  Unique GUIDs:  %d"), Result.TotalElements);
	UE_LOG(LogFragmentsUE, Log, TEXT("  Categories:    %d"), Result.Categories.Num());
	UE_LOG(LogFragmentsUE, Log, TEXT("═══════════════════════════════════════════════════"));

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

		if (I0 >= VertexCount || I1 >= VertexCount || I2 >= VertexCount)
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
