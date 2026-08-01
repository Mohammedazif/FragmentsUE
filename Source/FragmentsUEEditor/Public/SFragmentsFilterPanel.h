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

struct FFragFilterRow
{
	FString Name;
	int32 Count = 0;
	bool bVisible = true;
};

/** Holds no filter state — re-reads the actor each Refresh so external filtering stays in sync. */
class FRAGMENTSUEEDITOR_API SFragmentsFilterPanel : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SFragmentsFilterPanel) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	static const FName TabId;

private:
	void RefreshModelList();

	AFragmentsActor* GetSelectedModel() const;

	TSharedRef<SWidget> BuildModelPicker();
	FText GetSelectedModelName() const;

	void Refresh();

	void RebuildRowWidgets(const TSharedPtr<SVerticalBox>& Container, TArray<FFragFilterRow>& Rows, bool bIsStorey);

	bool IsRowVisible(const AFragmentsActor* Model, const TArray<int32>& LocalIds) const;

	void OnRowCheckStateChanged(ECheckBoxState NewState, FString RowName, bool bIsStorey);
	FReply OnIsolateClicked(FString RowName, bool bIsStorey);
	FReply OnShowAllClicked();
	FReply OnRefreshClicked();

	FText GetStatusText() const;

	TArray<TWeakObjectPtr<AFragmentsActor>> Models;
	TWeakObjectPtr<AFragmentsActor> SelectedModel;

	TArray<FFragFilterRow> StoreyRows;
	TArray<FFragFilterRow> CategoryRows;

	TSharedPtr<SVerticalBox> StoreyContainer;
	TSharedPtr<SVerticalBox> CategoryContainer;
};
