#include "AnimMontagePropertyPanel.h"

#include "Animation/Montage/AnimMontage.h"
#include "Animation/Montage/AnimMontageInstance.h"
#include "Animation/Sequence/AnimSequence.h"
#include "Animation/AnimInstance.h"
#include "Component/Primitive/SkeletalMeshComponent.h"
#include "Animation/AnimationManager.h"

#include <imgui.h>
#include <algorithm>
#include <cstdio>
#include <cstring>

namespace
{
	int32 FindSectionIndexByName(const TArray<FCompositeSection>& Sections, FName Name)
	{
		for (int32 i = 0; i < static_cast<int32>(Sections.size()); ++i)
		{
			if (Sections[i].SectionName == Name) return i;
		}
		return -1;
	}

	FName MakeUniqueSectionName(const TArray<FCompositeSection>& Sections, const char* Prefix)
	{
		for (int32 Candidate = 1; Candidate < 10000; ++Candidate)
		{
			char Buf[64];
			std::snprintf(Buf, sizeof(Buf), "%s%d", Prefix, Candidate);
			FName Name(Buf);
			if (FindSectionIndexByName(Sections, Name) < 0) return Name;
		}
		return FName("Section");
	}

	void RenameSectionCascade(TArray<FCompositeSection>& Sections, FName OldName, FName NewName)
	{
		if (OldName == NewName) return;
		for (FCompositeSection& S : Sections)
		{
			if (S.NextSectionName == OldName) S.NextSectionName = NewName;
		}
	}

	bool HasDuplicateSectionName(const TArray<FCompositeSection>& Sections, int32 Index)
	{
		if (Index < 0 || Index >= static_cast<int32>(Sections.size())) return false;
		for (int32 i = 0; i < static_cast<int32>(Sections.size()); ++i)
		{
			if (i == Index) continue;
			if (Sections[i].SectionName == Sections[Index].SectionName) return true;
		}
		return false;
	}

	float SectionDuration(const FCompositeSection& S)
	{
		return std::max(S.LinkTime - S.StartTime, 0.0f);
	}

	FString FormatNextSectionShort(const FCompositeSection& S)
	{
		if (S.NextSectionName == FName::None) return "End Montage";
		if (S.NextSectionName == S.SectionName) return "Loop Here";
		return FString("Go To ") + S.NextSectionName.ToString();
	}

	FString FormatNextSectionSentence(const FCompositeSection& S)
	{
		if (S.NextSectionName == FName::None)
		{
			return "then blend out and finish the montage.";
		}
		if (S.NextSectionName == S.SectionName)
		{
			return FString("then loop '") + S.SectionName.ToString() + "'.";
		}
		return FString("then jump to '") + S.NextSectionName.ToString() + "'.";
	}

	bool FullWidthButton(const char* Label)
	{
		return ImGui::Button(Label, ImVec2(-1.0f, 0.0f));
	}

	bool SplitButtonRow(const char* LeftLabel, const char* RightLabel, bool& bLeftClicked, bool& bRightClicked)
	{
		const float Spacing = ImGui::GetStyle().ItemSpacing.x;
		const float Width = ImGui::GetContentRegionAvail().x;
		const float Half = std::max(60.0f, (Width - Spacing) * 0.5f);
		bLeftClicked = ImGui::Button(LeftLabel, ImVec2(Half, 0.0f));
		ImGui::SameLine();
		bRightClicked = ImGui::Button(RightLabel, ImVec2(-1.0f, 0.0f));
		return bLeftClicked || bRightClicked;
	}

	void RenderMontageTimeline(UAnimMontage* Montage, UAnimInstance* PreviewInst)
	{
		const UAnimSequence* Src = Montage->GetSourceSequence();
		const float MaxTime = Src ? std::max(Src->GetPlayLength(), 0.0f) : 0.0f;
		const float Width = std::max(ImGui::GetContentRegionAvail().x, 80.0f);
		const float Height = 86.0f;

		ImVec2 P = ImGui::GetCursorScreenPos();
		ImGui::InvisibleButton("##MontageHumanTimeline", ImVec2(Width, Height));
		ImDrawList* DrawList = ImGui::GetWindowDrawList();
		if (!DrawList) return;

		const ImU32 BgColor      = IM_COL32(32, 34, 40, 255);
		const ImU32 BorderColor  = IM_COL32(86, 92, 108, 255);
		const ImU32 SectionColor = IM_COL32(116, 86, 158, 255);
		const ImU32 TextColor    = IM_COL32(235, 238, 242, 255);
		const ImU32 MutedColor   = IM_COL32(150, 156, 170, 255);
		const ImU32 PlayColor    = IM_COL32(255, 210, 90, 255);

		DrawList->AddRectFilled(P, ImVec2(P.x + Width, P.y + Height), BgColor, 6.0f);
		DrawList->AddRect(P, ImVec2(P.x + Width, P.y + Height), BorderColor, 6.0f);

		if (MaxTime <= 0.0f)
		{
			DrawList->AddText(ImVec2(P.x + 10.0f, P.y + 28.0f), MutedColor, "Select a source sequence to edit montage timing.");
			return;
		}

		const float TrackY0 = P.y + 34.0f;
		const float TrackY1 = P.y + 64.0f;
		for (int32 i = 0; i < static_cast<int32>(Montage->GetSections().size()); ++i)
		{
			const FCompositeSection& S = Montage->GetSections()[i];
			const float X0 = P.x + std::clamp(S.StartTime / MaxTime, 0.0f, 1.0f) * Width;
			const float X1 = P.x + std::clamp(S.LinkTime  / MaxTime, 0.0f, 1.0f) * Width;
			const float MinX1 = std::max(X1, X0 + 4.0f);
			DrawList->AddRectFilled(ImVec2(X0, TrackY0), ImVec2(MinX1, TrackY1), SectionColor, 4.0f);
			DrawList->AddRect(ImVec2(X0, TrackY0), ImVec2(MinX1, TrackY1), BorderColor, 4.0f);

			const FString SectionName = S.SectionName.ToString();
			const float LabelWidth = ImGui::CalcTextSize(SectionName.c_str()).x;
			if (MinX1 - X0 > LabelWidth + 10.0f)
			{
				DrawList->AddText(ImVec2(X0 + 5.0f, TrackY0 + 7.0f), TextColor, SectionName.c_str());
			}
		}

		char LeftLabel[32];
		char RightLabel[32];
		std::snprintf(LeftLabel, sizeof(LeftLabel), "0.00s");
		std::snprintf(RightLabel, sizeof(RightLabel), "%.2fs", MaxTime);
		DrawList->AddText(ImVec2(P.x + 8.0f, P.y + 8.0f), MutedColor, LeftLabel);
		const float RightW = ImGui::CalcTextSize(RightLabel).x;
		DrawList->AddText(ImVec2(P.x + Width - RightW - 8.0f, P.y + 8.0f), MutedColor, RightLabel);

		if (PreviewInst)
		{
			if (UAnimMontageInstance* MI = PreviewInst->GetMontageInstance())
			{
				if (MI->IsActive() && MI->GetCurrentMontage() == Montage)
				{
					const int32 SectionIdx = FindSectionIndexByName(Montage->GetSections(), MI->GetCurrentSectionName());
					if (SectionIdx >= 0)
					{
						const FCompositeSection& S = Montage->GetSections()[SectionIdx];
						const float SeqTime = std::clamp(S.StartTime + MI->GetSectionTime(), 0.0f, MaxTime);
						const float X = P.x + (SeqTime / MaxTime) * Width;
						DrawList->AddLine(ImVec2(X, P.y + 6.0f), ImVec2(X, P.y + Height - 6.0f), PlayColor, 2.0f);
					}
				}
			}
		}
	}

	void RenderMontageFlowPlan(UAnimMontage* Montage)
	{
		const auto& Sections = Montage->GetSections();
		if (Sections.empty())
		{
			ImGui::TextWrapped("No sections yet. Add one section, then decide what happens after it finishes.");
			return;
		}

		ImGui::TextUnformatted("Playback Plan");
		ImGui::Separator();
		ImGui::TextWrapped("Each card reads as: play this time range, then choose the next section or finish.");
		ImGui::Spacing();

		for (int32 i = 0; i < static_cast<int32>(Sections.size()); ++i)
		{
			const FCompositeSection& S = Sections[i];
			ImGui::PushID(i);
			ImGui::BeginChild("##FlowCard", ImVec2(0.0f, 104.0f), true);
			ImGui::TextColored(ImVec4(0.86f, 0.66f, 1.00f, 1.0f), "%s", S.SectionName.ToString().c_str());
			ImGui::TextWrapped("Play %.2fs to %.2fs. Duration: %.2fs.", S.StartTime, S.LinkTime, SectionDuration(S));
			ImGui::TextWrapped("After this: %s", FormatNextSectionShort(S).c_str());
			ImGui::TextDisabled("Meaning: %s", FormatNextSectionSentence(S).c_str());
			ImGui::EndChild();
			ImGui::Spacing();
			ImGui::PopID();
		}
	}

	bool RenderSourceSection(UAnimMontage* Montage)
	{
		bool bChanged = false;

		ImGui::TextUnformatted("Source Animation");
		ImGui::Separator();

		UAnimSequence* CurSrc = Montage->GetSourceSequence();
		const FString SrcLabel = CurSrc ? CurSrc->GetName() : FString("(none)");
		ImGui::SetNextItemWidth(-1.0f);
		if (ImGui::BeginCombo("##montageSrc", SrcLabel.c_str()))
		{
			if (ImGui::Selectable("(none)", CurSrc == nullptr))
			{
				Montage->SetSourceSequence(nullptr);
				Montage->NormalizeSections();
				bChanged = true;
			}
			const TArray<FAssetListItem>& Anims = FAnimationManager::Get().GetAvailableAnimationFiles();
			for (const FAssetListItem& Item : Anims)
			{
				const bool bSelected = (CurSrc && CurSrc->GetAssetPathFileName() == Item.FullPath);
				if (ImGui::Selectable(Item.DisplayName.c_str(), bSelected))
				{
					UAnimSequence* Loaded = FAnimationManager::Get().LoadAnimation(Item.FullPath);
					if (Loaded)
					{
						Montage->SetSourceSequence(Loaded);
						Montage->NormalizeSections();
						bChanged = true;
					}
				}
				if (bSelected) ImGui::SetItemDefaultFocus();
			}
			ImGui::EndCombo();
		}

		if (CurSrc)
		{
			ImGui::TextWrapped("Length: %.3f seconds", CurSrc->GetPlayLength());
		}
		else
		{
			ImGui::TextWrapped("A montage needs a source animation before section times are meaningful.");
		}

		return bChanged;
	}

	bool RenderBlendSection(UAnimMontage* Montage)
	{
		bool bChanged = false;

		ImGui::Dummy(ImVec2(0, 6));
		ImGui::TextUnformatted("Blend Times");
		ImGui::Separator();

		float BlendIn  = Montage->GetBlendInTime();
		float BlendOut = Montage->GetBlendOutTime();

		ImGui::TextUnformatted("Blend In");
		ImGui::SetNextItemWidth(-1.0f);
		if (ImGui::DragFloat("##BlendInMontage", &BlendIn, 0.01f, 0.0f, 5.0f, "%.2f s"))
		{
			Montage->SetBlendInTime(BlendIn);
			bChanged = true;
		}

		ImGui::TextUnformatted("Blend Out");
		ImGui::SetNextItemWidth(-1.0f);
		if (ImGui::DragFloat("##BlendOutMontage", &BlendOut, 0.01f, 0.0f, 5.0f, "%.2f s"))
		{
			Montage->SetBlendOutTime(BlendOut);
			bChanged = true;
		}

		return bChanged;
	}

	bool RenderSectionNextCombo(FCompositeSection& S, const TArray<FCompositeSection>& Sections)
	{
		bool bChanged = false;
		const FString NextPreview = FormatNextSectionShort(S);
		ImGui::SetNextItemWidth(-1.0f);
		if (ImGui::BeginCombo("##NextSection", NextPreview.c_str()))
		{
			if (ImGui::Selectable("End Montage", S.NextSectionName == FName::None))
			{
				S.NextSectionName = FName::None;
				bChanged = true;
			}
			if (ImGui::Selectable("Loop This Section", S.NextSectionName == S.SectionName))
			{
				S.NextSectionName = S.SectionName;
				bChanged = true;
			}
			ImGui::Separator();
			for (const FCompositeSection& Cand : Sections)
			{
				const FString CandName = FString("Go To ") + Cand.SectionName.ToString();
				const bool bSel = (S.NextSectionName == Cand.SectionName) && (S.NextSectionName != S.SectionName);
				if (ImGui::Selectable(CandName.c_str(), bSel))
				{
					S.NextSectionName = Cand.SectionName;
					bChanged = true;
				}
			}
			ImGui::EndCombo();
		}
		return bChanged;
	}

	bool RenderSectionsEditor(UAnimMontage* Montage)
	{
		ImGui::Dummy(ImVec2(0, 6));
		ImGui::TextUnformatted("Sections");
		ImGui::Separator();

		auto& Sections = Montage->GetMutableSections();
		const UAnimSequence* Src = Montage->GetSourceSequence();
		const float MaxTime = Src ? std::max(Src->GetPlayLength(), 0.0f) : 0.0f;
		bool bChanged = false;
		bool bNeedsNormalize = false;

		if (FullWidthButton("+ Add Section"))
		{
			FCompositeSection NewS;
			NewS.SectionName = MakeUniqueSectionName(Sections, "Section_");
			NewS.StartTime = 0.0f;
			NewS.LinkTime  = MaxTime;
			if (!Sections.empty())
			{
				NewS.StartTime = std::clamp(Sections.back().LinkTime, 0.0f, MaxTime);
				if (NewS.StartTime >= MaxTime && MaxTime > 0.0f)
				{
					NewS.StartTime = 0.0f;
				}
				NewS.LinkTime = MaxTime;
			}
			Sections.push_back(NewS);
			bChanged = true;
			bNeedsNormalize = true;
		}

		bool bAutoChain = false;
		bool bSort = false;
		SplitButtonRow("Flow = Timeline", "Sort by Time", bAutoChain, bSort);
		if (bSort)
		{
			std::sort(Sections.begin(), Sections.end(), [](const FCompositeSection& A, const FCompositeSection& B)
			{
				return A.StartTime < B.StartTime;
			});
			bChanged = true;
		}
		if (bAutoChain)
		{
			std::sort(Sections.begin(), Sections.end(), [](const FCompositeSection& A, const FCompositeSection& B)
			{
				return A.StartTime < B.StartTime;
			});
			for (int32 i = 0; i < static_cast<int32>(Sections.size()); ++i)
			{
				Sections[i].NextSectionName = (i + 1 < static_cast<int32>(Sections.size()))
					? Sections[i + 1].SectionName
					: FName::None;
			}
			bChanged = true;
		}

		ImGui::Spacing();
		RenderMontageFlowPlan(Montage);
		ImGui::Spacing();

		int32 RemoveIdx = -1;
		for (int32 i = 0; i < static_cast<int32>(Sections.size()); ++i)
		{
			FCompositeSection& S = Sections[i];
			ImGui::PushID(i);

			char Header[128];
			std::snprintf(Header, sizeof(Header), "%s  %.2fs###MontageSection%d",
				S.SectionName.ToString().c_str(), SectionDuration(S), i);
			if (ImGui::CollapsingHeader(Header, ImGuiTreeNodeFlags_DefaultOpen))
			{
				ImGui::TextWrapped("Play %.2fs to %.2fs, %s", S.StartTime, S.LinkTime, FormatNextSectionSentence(S).c_str());

				const bool bDuplicate = HasDuplicateSectionName(Sections, i);
				if (bDuplicate)
				{
					ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.35f, 1.0f), "Name is duplicated. Runtime section lookup needs unique names.");
				}

				char NameBuf[64];
				std::snprintf(NameBuf, sizeof(NameBuf), "%s", S.SectionName.ToString().c_str());
				ImGui::TextUnformatted("Section Name");
				ImGui::SetNextItemWidth(-1.0f);
				if (ImGui::InputText("##SectionName", NameBuf, sizeof(NameBuf)))
				{
					const FName OldName = S.SectionName;
					S.SectionName = (NameBuf[0] == '\0') ? MakeUniqueSectionName(Sections, "Section_") : FName(NameBuf);
					RenameSectionCascade(Sections, OldName, S.SectionName);
					bChanged = true;
				}

				const float W = ImGui::GetContentRegionAvail().x;
				if (W >= 240.0f && ImGui::BeginTable("##SectionTimes", 2, ImGuiTableFlags_SizingStretchSame))
				{
					ImGui::TableNextColumn();
					ImGui::TextUnformatted("Start Time");
					ImGui::SetNextItemWidth(-1.0f);
					if (ImGui::DragFloat("##Start", &S.StartTime, 0.01f, 0.0f, MaxTime, "%.2f s"))
					{
						bChanged = true;
						bNeedsNormalize = true;
					}
					ImGui::TableNextColumn();
					ImGui::TextUnformatted("End Time");
					ImGui::SetNextItemWidth(-1.0f);
					if (ImGui::DragFloat("##End", &S.LinkTime, 0.01f, S.StartTime, MaxTime, "%.2f s"))
					{
						bChanged = true;
						bNeedsNormalize = true;
					}
					ImGui::EndTable();
				}
				else
				{
					ImGui::TextUnformatted("Start Time");
					ImGui::SetNextItemWidth(-1.0f);
					if (ImGui::DragFloat("##Start", &S.StartTime, 0.01f, 0.0f, MaxTime, "%.2f s"))
					{
						bChanged = true;
						bNeedsNormalize = true;
					}
					ImGui::TextUnformatted("End Time");
					ImGui::SetNextItemWidth(-1.0f);
					if (ImGui::DragFloat("##End", &S.LinkTime, 0.01f, S.StartTime, MaxTime, "%.2f s"))
					{
						bChanged = true;
						bNeedsNormalize = true;
					}
				}

				ImGui::TextDisabled("Duration: %.2f seconds", SectionDuration(S));
				ImGui::TextUnformatted("After This Section");
				if (RenderSectionNextCombo(S, Sections))
				{
					bChanged = true;
				}

				bool bSplit = false;
				bool bDelete = false;
				SplitButtonRow("Split Middle", "Delete", bSplit, bDelete);
				if (bSplit)
				{
					const float Mid = S.StartTime + (S.LinkTime - S.StartTime) * 0.5f;
					if (Mid > S.StartTime && Mid < S.LinkTime)
					{
						FCompositeSection NewS;
						NewS.SectionName = MakeUniqueSectionName(Sections, "Section_");
						NewS.StartTime = Mid;
						NewS.LinkTime  = S.LinkTime;
						NewS.NextSectionName = S.NextSectionName;
						S.LinkTime = Mid;
						S.NextSectionName = NewS.SectionName;
						Sections.insert(Sections.begin() + i + 1, NewS);
						bChanged = true;
						bNeedsNormalize = true;
					}
				}
				if (bDelete)
				{
					RemoveIdx = i;
				}
			}

			ImGui::PopID();
		}

		if (RemoveIdx >= 0)
		{
			const FName DeletedName = Sections[RemoveIdx].SectionName;
			Sections.erase(Sections.begin() + RemoveIdx);
			for (FCompositeSection& S : Sections)
			{
				if (S.NextSectionName == DeletedName) S.NextSectionName = FName::None;
			}
			bChanged = true;
			bNeedsNormalize = true;
		}

		if (bNeedsNormalize)
		{
			Montage->NormalizeSections();
		}

		return bChanged;
	}

	void RenderPreviewSection(UAnimMontage* Montage, USkeletalMeshComponent* PreviewComp, UAnimInstance* PreviewInst)
	{
		ImGui::Dummy(ImVec2(0, 6));
		ImGui::TextUnformatted("Preview");
		ImGui::Separator();

		if (!PreviewInst)
		{
			ImGui::TextWrapped("AnimInstance is not available, so montage preview cannot be played here.");
			return;
		}

		const auto& Sections = Montage->GetSections();
		static int32 sStartSectionIdx = 0;
		if (sStartSectionIdx >= static_cast<int32>(Sections.size())) sStartSectionIdx = 0;

		ImGui::TextUnformatted("Start Section");
		ImGui::SetNextItemWidth(-1.0f);
		const FString StartLabel = Sections.empty() ? FString("(none)") : Sections[sStartSectionIdx].SectionName.ToString();
		if (ImGui::BeginCombo("##startSection", StartLabel.c_str()))
		{
			for (int32 i = 0; i < static_cast<int32>(Sections.size()); ++i)
			{
				const bool bSel = (sStartSectionIdx == i);
				const FString Name = Sections[i].SectionName.ToString();
				if (ImGui::Selectable(Name.c_str(), bSel))
				{
					sStartSectionIdx = i;
				}
				if (bSel) ImGui::SetItemDefaultFocus();
			}
			ImGui::EndCombo();
		}

		bool bPlay = false;
		bool bStop = false;
		SplitButtonRow("Play", "Stop", bPlay, bStop);
		if (bPlay)
		{
			const FName Start = Sections.empty() ? FName::None : Sections[sStartSectionIdx].SectionName;
			PreviewInst->PlayMontage(Montage, Start);
			if (PreviewComp) PreviewComp->SetPlaying(true);
		}
		if (bStop)
		{
			PreviewInst->StopMontage();
		}

		ImGui::Dummy(ImVec2(0, 4));
		ImGui::TextUnformatted("Jump To Section");
		float LineUsed = 0.0f;
		const float MaxLine = ImGui::GetContentRegionAvail().x;
		for (int32 i = 0; i < static_cast<int32>(Sections.size()); ++i)
		{
			const FString Name = Sections[i].SectionName.ToString();
			const float BtnW = ImGui::CalcTextSize(Name.c_str()).x + ImGui::GetStyle().FramePadding.x * 2.0f + 12.0f;
			if (i > 0 && LineUsed + BtnW < MaxLine)
			{
				ImGui::SameLine();
				LineUsed += ImGui::GetStyle().ItemSpacing.x;
			}
			else
			{
				LineUsed = 0.0f;
			}
			ImGui::PushID(i);
			if (ImGui::SmallButton(Name.c_str()))
			{
				PreviewInst->Montage_JumpToSection(Sections[i].SectionName);
			}
			ImGui::PopID();
			LineUsed += BtnW;
		}

		ImGui::Dummy(ImVec2(0, 4));
		if (UAnimMontageInstance* MI = PreviewInst->GetMontageInstance())
		{
			if (MI->IsActive())
			{
				ImGui::TextWrapped("State: %s", MI->IsBlendingOut() ? "Blend Out" : "Playing");
				ImGui::TextWrapped("Section: %s", MI->GetCurrentSectionName().ToString().c_str());
				ImGui::TextWrapped("Section Time: %.2f   Weight: %.2f", MI->GetSectionTime(), MI->GetBlendWeight());
			}
			else
			{
				ImGui::TextDisabled("Inactive");
			}
		}
	}
}

bool FAnimMontagePropertyPanel::Render(UAnimMontage* Montage,
                                       USkeletalMeshComponent* PreviewComp,
                                       UAnimInstance* PreviewInst)
{
	if (!Montage)
	{
		ImGui::TextDisabled("No montage selected.");
		return false;
	}

	bool bChanged = false;

	ImGui::TextUnformatted("Animation Montage");
	ImGui::Separator();
	ImGui::TextWrapped("Name: %s", Montage->GetName().c_str());

	const FString& Path = Montage->GetAssetPathFileName();
	if (!Path.empty() && Path != "None")
	{
		ImGui::TextWrapped("Path: %s", Path.c_str());
	}

	ImGui::Dummy(ImVec2(0, 8));

	ImGui::TextUnformatted("Timeline");
	ImGui::Separator();
	RenderMontageTimeline(Montage, PreviewInst);

	if (RenderSourceSection(Montage)) bChanged = true;
	if (RenderBlendSection(Montage)) bChanged = true;
	if (RenderSectionsEditor(Montage)) bChanged = true;
	RenderPreviewSection(Montage, PreviewComp, PreviewInst);
	return bChanged;
}
