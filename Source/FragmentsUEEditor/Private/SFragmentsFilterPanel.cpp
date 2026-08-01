// Copyright (c) 2026 Mohammed Azif. Licensed under the MIT License — see the LICENSE file.

#include "SFragmentsFilterPanel.h"
#include "FragmentsActor.h"

#include "Editor.h"
#include "EngineUtils.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Styling/AppStyle.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "FragmentsFilterPanel"

const FName SFragmentsFilterPanel::TabId(TEXT("FragmentsUEFilter"));

namespace
{
	TArray<FFragFilterRow> MakeRows(const TMap<FString, int32>& Counts)
	{
		TArray<FFragFilterRow> Rows;
		Rows.Reserve(Counts.Num());

		for (const TPair<FString, int32>& Pair : Counts)
		{
			FFragFilterRow Row;
			Row.Name = Pair.Key;
			Row.Count = Pair.Value;
			Rows.Add(MoveTemp(Row));
		}

		// Biggest first, then alphabetically — an IFC file's long tail of one-off categories is noise.
		Rows.Sort([](const FFragFilterRow& A, const FFragFilterRow& B)
		{
			return A.Count != B.Count ? A.Count > B.Count : A.Name < B.Name;
		});

		return Rows;
	}
}

void SFragmentsFilterPanel::Construct(const FArguments& InArgs)
{
	RefreshModelList();

	ChildSlot
	[
		SNew(SVerticalBox)

		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(8.0f, 8.0f, 8.0f, 4.0f)
		[
			SNew(SHorizontalBox)

			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.VAlign(VAlign_Center)
			[
				BuildModelPicker()
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(4.0f, 0.0f, 0.0f, 0.0f)
			[
				SNew(SButton)
				.Text(LOCTEXT("Refresh", "Refresh"))
				.ToolTipText(LOCTEXT("RefreshTip", "Re-read the models in this level and their current filter state."))
				.OnClicked(this, &SFragmentsFilterPanel::OnRefreshClicked)
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(4.0f, 0.0f, 0.0f, 0.0f)
			[
				SNew(SButton)
				.Text(LOCTEXT("ShowAll", "Show All"))
				.ToolTipText(LOCTEXT("ShowAllTip", "Clear the filter and bring the whole model back."))
				.OnClicked(this, &SFragmentsFilterPanel::OnShowAllClicked)
			]
		]

		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(8.0f, 0.0f, 8.0f, 4.0f)
		[
			SNew(STextBlock)
			.Text(this, &SFragmentsFilterPanel::GetStatusText)
			.ColorAndOpacity(FSlateColor::UseSubduedForeground())
		]

		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(SSeparator)
		]

		+ SVerticalBox::Slot()
		.FillHeight(1.0f)
		[
			SNew(SScrollBox)

			+ SScrollBox::Slot()
			.Padding(8.0f, 8.0f, 8.0f, 2.0f)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("Levels", "Levels"))
				.Font(FAppStyle::GetFontStyle("DetailsView.CategoryFontStyle"))
			]

			+ SScrollBox::Slot()
			.Padding(8.0f, 0.0f, 8.0f, 8.0f)
			[
				SAssignNew(StoreyContainer, SVerticalBox)
			]

			+ SScrollBox::Slot()
			.Padding(8.0f, 8.0f, 8.0f, 2.0f)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("Categories", "Categories"))
				.Font(FAppStyle::GetFontStyle("DetailsView.CategoryFontStyle"))
			]

			+ SScrollBox::Slot()
			.Padding(8.0f, 0.0f, 8.0f, 8.0f)
			[
				SAssignNew(CategoryContainer, SVerticalBox)
			]
		]
	];

	Refresh();
}

void SFragmentsFilterPanel::RefreshModelList()
{
	Models.Reset();

	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World)
	{
		SelectedModel = nullptr;
		return;
	}

	for (TActorIterator<AFragmentsActor> It(World); It; ++It)
	{
		Models.Add(*It);
	}

	if (!SelectedModel.IsValid() || !Models.Contains(SelectedModel))
	{
		SelectedModel = Models.Num() > 0 ? Models[0] : nullptr;
	}
}

AFragmentsActor* SFragmentsFilterPanel::GetSelectedModel() const
{
	return SelectedModel.Get();
}

FText SFragmentsFilterPanel::GetSelectedModelName() const
{
	const AFragmentsActor* Model = GetSelectedModel();
	if (!Model)
	{
		return LOCTEXT("NoModel", "No Fragments model in this level");
	}

	return Model->ModelName.IsEmpty()
		? FText::FromString(Model->GetName())
		: FText::FromString(Model->ModelName);
}

TSharedRef<SWidget> SFragmentsFilterPanel::BuildModelPicker()
{
	return SNew(SComboButton)
		.ButtonContent()
		[
			SNew(STextBlock).Text(this, &SFragmentsFilterPanel::GetSelectedModelName)
		]
		.OnGetMenuContent_Lambda([this]()
		{
			RefreshModelList();

			FMenuBuilder MenuBuilder(true, nullptr);
			for (const TWeakObjectPtr<AFragmentsActor>& Model : Models)
			{
				if (!Model.IsValid())
				{
					continue;
				}

				const FString Label = Model->ModelName.IsEmpty() ? Model->GetName() : Model->ModelName;
				TWeakObjectPtr<AFragmentsActor> Captured = Model;

				MenuBuilder.AddMenuEntry(
					FText::FromString(Label),
					FText::GetEmpty(),
					FSlateIcon(),
					FUIAction(FExecuteAction::CreateLambda([this, Captured]()
					{
						SelectedModel = Captured;
						Refresh();
					})));
			}

			return MenuBuilder.MakeWidget();
		});
}

bool SFragmentsFilterPanel::IsRowVisible(const AFragmentsActor* Model, const TArray<int32>& LocalIds) const
{
	if (!Model)
	{
		return true;
	}

	for (const int32 LocalId : LocalIds)
	{
		if (Model->IsLocalIdHidden(LocalId))
		{
			return false;
		}
	}
	return true;
}

void SFragmentsFilterPanel::Refresh()
{
	AFragmentsActor* Model = GetSelectedModel();

	StoreyRows.Reset();
	CategoryRows.Reset();

	if (Model)
	{
		StoreyRows = MakeRows(Model->GetStoreyCounts());
		CategoryRows = MakeRows(Model->GetCategoryCounts());

		for (FFragFilterRow& Row : StoreyRows)
		{
			Row.bVisible = IsRowVisible(Model, Model->FindItemsByStorey(Row.Name));
		}
		for (FFragFilterRow& Row : CategoryRows)
		{
			Row.bVisible = IsRowVisible(Model, Model->FindItemsByCategory(Row.Name));
		}
	}

	RebuildRowWidgets(StoreyContainer, StoreyRows, /*bIsStorey*/ true);
	RebuildRowWidgets(CategoryContainer, CategoryRows, /*bIsStorey*/ false);
}

void SFragmentsFilterPanel::RebuildRowWidgets(const TSharedPtr<SVerticalBox>& Container, TArray<FFragFilterRow>& Rows, bool bIsStorey)
{
	if (!Container.IsValid())
	{
		return;
	}

	Container->ClearChildren();

	if (Rows.Num() == 0)
	{
		Container->AddSlot()
		.AutoHeight()
		[
			SNew(STextBlock)
			.Text(LOCTEXT("Empty", "Nothing here — import a model, or check that metadata import was on."))
			.ColorAndOpacity(FSlateColor::UseSubduedForeground())
			.AutoWrapText(true)
		];
		return;
	}

	for (int32 Index = 0; Index < Rows.Num(); ++Index)
	{
		const FString RowName = Rows[Index].Name;
		const int32 RowCount = Rows[Index].Count;
		const bool bRowVisible = Rows[Index].bVisible;

		Container->AddSlot()
		.AutoHeight()
		.Padding(0.0f, 1.0f)
		[
			SNew(SHorizontalBox)

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				SNew(SCheckBox)
				.IsChecked(bRowVisible ? ECheckBoxState::Checked : ECheckBoxState::Unchecked)
				.ToolTipText(LOCTEXT("ToggleTip", "Show or hide this group."))
				.OnCheckStateChanged(this, &SFragmentsFilterPanel::OnRowCheckStateChanged, RowName, bIsStorey)
			]

			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.VAlign(VAlign_Center)
			.Padding(4.0f, 0.0f, 0.0f, 0.0f)
			[
				SNew(STextBlock).Text(FText::FromString(RowName))
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(4.0f, 0.0f, 8.0f, 0.0f)
			[
				SNew(STextBlock)
				.Text(FText::AsNumber(RowCount))
				.ColorAndOpacity(FSlateColor::UseSubduedForeground())
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				SNew(SButton)
				.Text(LOCTEXT("Isolate", "Isolate"))
				.ToolTipText(LOCTEXT("IsolateTip", "Show only this group and hide everything else."))
				.OnClicked(this, &SFragmentsFilterPanel::OnIsolateClicked, RowName, bIsStorey)
			]
		];
	}
}

void SFragmentsFilterPanel::OnRowCheckStateChanged(ECheckBoxState NewState, FString RowName, bool bIsStorey)
{
	AFragmentsActor* Model = GetSelectedModel();
	if (!Model)
	{
		return;
	}

	const bool bVisible = NewState == ECheckBoxState::Checked;

	if (bIsStorey)
	{
		Model->SetStoreyVisible(RowName, bVisible);
	}
	else
	{
		Model->SetCategoryVisible(RowName, bVisible);
	}

	// A level and a category overlap, so toggling one changes what the other shows.
	Refresh();
}

FReply SFragmentsFilterPanel::OnIsolateClicked(FString RowName, bool bIsStorey)
{
	if (AFragmentsActor* Model = GetSelectedModel())
	{
		if (bIsStorey)
		{
			Model->IsolateByStorey(RowName);
		}
		else
		{
			Model->IsolateByCategory(RowName);
		}
		Refresh();
	}

	return FReply::Handled();
}

FReply SFragmentsFilterPanel::OnShowAllClicked()
{
	if (AFragmentsActor* Model = GetSelectedModel())
	{
		Model->ClearFilter();
		Refresh();
	}

	return FReply::Handled();
}

FReply SFragmentsFilterPanel::OnRefreshClicked()
{
	RefreshModelList();
	Refresh();
	return FReply::Handled();
}

FText SFragmentsFilterPanel::GetStatusText() const
{
	const AFragmentsActor* Model = GetSelectedModel();
	if (!Model)
	{
		return LOCTEXT("StatusNoModel", "Drop a .frag model into the level to filter it.");
	}

	if (!Model->SupportsFiltering())
	{
		return LOCTEXT("StatusNoHierarchy",
			"This model was imported without a hierarchy, so there are no actors to hide. "
			"Re-import in one of the Hierarchy modes to filter it.");
	}

	if (!Model->SupportsElementFiltering())
	{
		return LOCTEXT("StatusStoreyOnly",
			"Imported merged per storey — levels can be filtered, single elements cannot.");
	}

	const int32 Hidden = Model->GetHiddenCount();
	if (Hidden == 0)
	{
		return LOCTEXT("StatusWhole", "Showing the whole model.");
	}

	return FText::Format(LOCTEXT("StatusHiding", "Hiding {0} elements."), FText::AsNumber(Hidden));
}

#undef LOCTEXT_NAMESPACE
