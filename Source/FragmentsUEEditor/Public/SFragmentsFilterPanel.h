// Copyright (c) 2026 Mohammed Azif. Licensed under the MIT License — see the LICENSE file.

#pragma once

#include "CoreMinimal.h"
#include "Input/Reply.h"
#include "Styling/SlateTypes.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/DeclarativeSyntaxSupport.h"

class AFragmentsActor;
class SVerticalBox;
class STextBlock;
class SWidget;

/** One level or category row: what it is, how big it is, whether it is showing. */
struct FFragFilterRow
{
	FString Name;
	int32 Count = 0;
	bool bVisible = true;
};

/**
 * Level, category and property filtering for an imported model.
 *
 * Reads the IFC metadata already on the AFragmentsActor and drives its filtering
 * API — the panel holds no filter state of its own, so a model filtered from
 * Blueprint or the console still shows up here correctly on Refresh.
 */
class FRAGMENTSUEEDITOR_API SFragmentsFilterPanel : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SFragmentsFilterPanel) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	/** Tab id this panel is registered under. */
	static const FName TabId;

private:
	// ── Model selection ────────────────────────────────────────────────────────

	/** Every AFragmentsActor in the editor world, newest import last. */
	void RefreshModelList();

	/** The model the panel is driving, or null when the level has none. */
	AFragmentsActor* GetSelectedModel() const;

	TSharedRef<SWidget> BuildModelPicker();
	FText GetSelectedModelName() const;

	// ── Rows ───────────────────────────────────────────────────────────────────

	/** Re-read levels, categories and hidden state from the model. */
	void Refresh();

	/** Rebuild one of the two row lists into its container. */
	void RebuildRowWidgets(const TSharedPtr<SVerticalBox>& Container, TArray<FFragFilterRow>& Rows, bool bIsStorey);

	/** Whether every id behind this row is currently visible. */
	bool IsRowVisible(const AFragmentsActor* Model, const TArray<int32>& LocalIds) const;

	void OnRowCheckStateChanged(ECheckBoxState NewState, FString RowName, bool bIsStorey);
	FReply OnIsolateClicked(FString RowName, bool bIsStorey);
	FReply OnShowAllClicked();
	FReply OnRefreshClicked();

	FText GetStatusText() const;

	// ── State ──────────────────────────────────────────────────────────────────

	TArray<TWeakObjectPtr<AFragmentsActor>> Models;
	TWeakObjectPtr<AFragmentsActor> SelectedModel;

	TArray<FFragFilterRow> StoreyRows;
	TArray<FFragFilterRow> CategoryRows;

	TSharedPtr<SVerticalBox> StoreyContainer;
	TSharedPtr<SVerticalBox> CategoryContainer;
};
