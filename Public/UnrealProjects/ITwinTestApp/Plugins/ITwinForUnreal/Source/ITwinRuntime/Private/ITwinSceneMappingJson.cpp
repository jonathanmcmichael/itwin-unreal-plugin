/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinSceneMappingJson.cpp $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#include "ITwinSceneMapping.h"
#include <Dom/JsonObject.h>
#include <Dom/JsonValue.h>
#include <Serialization/JsonReader.h>
#include <Serialization/JsonSerializer.h>
#include <Serialization/JsonWriter.h>

namespace
{
	FString AnimKeyToJson(FIModelElementsKey const& Key)
	{
		struct FKeyVisitor
		{
			FString operator()(ITwinElementID const& ElemID) const
			{
				return FString::Printf(TEXT("e:%s"), *ITwin::ToString(ElemID));
			}
			FString operator()(size_t const& GroupIdx) const
			{
				return FString::Printf(TEXT("g:%llu"), (uint64)GroupIdx);
			}
			FString operator()(FGuid const& Guid) const
			{
				return FString(TEXT("u:")) + Guid.ToString();
			}
		};
		return std::visit(FKeyVisitor{}, Key.Key);
	}

	FIModelElementsKey AnimKeyFromJson(FString const& Str)
	{
		if (Str.StartsWith(TEXT("e:")))
		{
			return FIModelElementsKey(ITwin::ParseElementID(*Str + 2));
		}
		else if (Str.StartsWith(TEXT("g:")))
		{
			uint64 Val = FCString::Strtoui64(*Str + 2, nullptr, 10);
			return FIModelElementsKey((size_t)Val);
		}
		else // "u:"
		{
			FGuid Guid;
			FGuid::Parse(Str.Mid(2), Guid);
			return FIModelElementsKey(Guid);
		}
	}
} // anonymous namespace

TSharedPtr<FJsonObject> FITwinSceneMapping::ToJson() const
{
	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	TArray<TSharedPtr<FJsonValue>> ElementsArray;

	auto const& ByRank = AllElements.get<IndexByRank>();
	for (auto const& Elem : ByRank)
	{
		TSharedPtr<FJsonObject> ElemObj = MakeShared<FJsonObject>();
		ElemObj->SetStringField(TEXT("id"), ITwin::ToString(Elem.ElementID));

		if (Elem.ParentInVec != ITwinScene::NOT_ELEM)
		{
			ElemObj->SetNumberField(TEXT("parent"), (double)Elem.ParentInVec.value());
		}
		if (Elem.DuplicatesList != ITwinScene::NOT_DUPL)
		{
			ElemObj->SetNumberField(TEXT("duplicates"), (double)Elem.DuplicatesList.value());
		}

		if (Elem.BBox.IsValid)
		{
			TArray<TSharedPtr<FJsonValue>> BBoxArr;
			BBoxArr.Add(MakeShared<FJsonValueNumber>(Elem.BBox.Min.X));
			BBoxArr.Add(MakeShared<FJsonValueNumber>(Elem.BBox.Min.Y));
			BBoxArr.Add(MakeShared<FJsonValueNumber>(Elem.BBox.Min.Z));
			BBoxArr.Add(MakeShared<FJsonValueNumber>(Elem.BBox.Max.X));
			BBoxArr.Add(MakeShared<FJsonValueNumber>(Elem.BBox.Max.Y));
			BBoxArr.Add(MakeShared<FJsonValueNumber>(Elem.BBox.Max.Z));
			ElemObj->SetArrayField(TEXT("bbox"), BBoxArr);
		}

		if (!Elem.AnimationKeys.empty())
		{
			TArray<TSharedPtr<FJsonValue>> KeysArr;
			for (auto const& Key : Elem.AnimationKeys)
			{
				KeysArr.Add(MakeShared<FJsonValueString>(AnimKeyToJson(Key)));
			}
			ElemObj->SetArrayField(TEXT("animKeys"), KeysArr);
		}

		if (!Elem.SubElemsInVec.empty())
		{
			TArray<TSharedPtr<FJsonValue>> SubArr;
			for (auto const& Sub : Elem.SubElemsInVec)
			{
				SubArr.Add(MakeShared<FJsonValueNumber>((double)Sub.value()));
			}
			ElemObj->SetArrayField(TEXT("subElems"), SubArr);
		}

		ElementsArray.Add(MakeShared<FJsonValueObject>(ElemObj));
	}

	Root->SetArrayField(TEXT("elements"), ElementsArray);

	// Serialize FederatedElementGUIDs: sort by rank to compare json strings in unit tests
	{
		TArray<TSharedPtr<FJsonValue>> GuidsArray;
		std::set<FElemGuid> FedGUIDsByRank;
		for (auto const& Entry : FederatedElementGUIDs)
			FedGUIDsByRank.insert(Entry);
		for (auto const& Entry : FedGUIDsByRank)
		{
			TSharedPtr<FJsonObject> GuidObj = MakeShared<FJsonObject>();
			GuidObj->SetNumberField(TEXT("rank"), (double)Entry.Rank.value());
			GuidObj->SetStringField(TEXT("guid"), Entry.FederatedGuid.ToString());
			GuidsArray.Add(MakeShared<FJsonValueObject>(GuidObj));
		}
		Root->SetArrayField(TEXT("federatedElementGUIDs"), GuidsArray);
	}

	// Serialize DuplicateElements
	{
		TArray<TSharedPtr<FJsonValue>> DuplArray;
		for (auto const& DuplVec : DuplicateElements)
		{
			TArray<TSharedPtr<FJsonValue>> IndicesArr;
			for (auto const& Idx : DuplVec)
			{
				IndicesArr.Add(MakeShared<FJsonValueNumber>((double)Idx.value()));
			}
			DuplArray.Add(MakeShared<FJsonValueArray>(IndicesArr));
		}
		Root->SetArrayField(TEXT("duplicateElements"), DuplArray);
	}

	// Serialize ConstructionDetailingParentsToHide
	{
		TArray<TSharedPtr<FJsonValue>> ConstrArr;
		for (auto const& Idx : ConstructionDetailingParentsToHide)
		{
			ConstrArr.Add(MakeShared<FJsonValueNumber>((double)Idx.value()));
		}
		Root->SetArrayField(TEXT("constructionDetailingParentsToHide"), ConstrArr);
	}

	return Root;
}

bool FITwinSceneMapping::FromJson(TSharedPtr<FJsonObject> const& Root)
{
	if (!Root.IsValid())
		return false;

	TArray<TSharedPtr<FJsonValue>> const* ElementsArray = nullptr;
	if (!Root->TryGetArrayField(TEXT("elements"), ElementsArray) || !ElementsArray)
		return false;

	ITwinScene::ElemIdx NextIdx(0);
	for (auto const& Val : *ElementsArray)
	{
		TSharedPtr<FJsonObject> const ElemObj = Val->AsObject();
		if (!ElemObj.IsValid())
			return false;

		FString IdStr;
		if (!ElemObj->TryGetStringField(TEXT("id"), IdStr))
			return false;
		ITwinElementID const ElemID = ITwin::ParseElementID(IdStr);
		FITwinElement& Element = ElementForSLOW(ElemID);

		double ParentVal = 0;
		if (ElemObj->TryGetNumberField(TEXT("parent"), ParentVal))
			Element.ParentInVec = ITwinScene::ElemIdx((uint64)ParentVal);

		double DuplVal = 0;
		if (ElemObj->TryGetNumberField(TEXT("duplicates"), DuplVal))
			Element.DuplicatesList = ITwinScene::DuplIdx((uint64)DuplVal);

		TArray<TSharedPtr<FJsonValue>> const* BBoxArr = nullptr;
		if (ElemObj->TryGetArrayField(TEXT("bbox"), BBoxArr) && BBoxArr && BBoxArr->Num() == 6)
		{
			Element.BBox.Min.X = (*BBoxArr)[0]->AsNumber();
			Element.BBox.Min.Y = (*BBoxArr)[1]->AsNumber();
			Element.BBox.Min.Z = (*BBoxArr)[2]->AsNumber();
			Element.BBox.Max.X = (*BBoxArr)[3]->AsNumber();
			Element.BBox.Max.Y = (*BBoxArr)[4]->AsNumber();
			Element.BBox.Max.Z = (*BBoxArr)[5]->AsNumber();
			Element.BBox.IsValid = 1;
		}

		TArray<TSharedPtr<FJsonValue>> const* KeysArr = nullptr;
		if (ElemObj->TryGetArrayField(TEXT("animKeys"), KeysArr) && KeysArr)
		{
			for (auto const& KeyVal : *KeysArr)
			{
				Element.AnimationKeys.push_back(AnimKeyFromJson(KeyVal->AsString()));
			}
		}

		TArray<TSharedPtr<FJsonValue>> const* SubArr = nullptr;
		if (ElemObj->TryGetArrayField(TEXT("subElems"), SubArr) && SubArr)
		{
			for (auto const& SubVal : *SubArr)
			{
				Element.SubElemsInVec.push_back(ITwinScene::ElemIdx((uint64)SubVal->AsNumber()));
			}
		}

		NextIdx = ITwinScene::ElemIdx(NextIdx.value() + 1);
	}

	// Deserialize FederatedElementGUIDs
	{
		TArray<TSharedPtr<FJsonValue>> const* GuidsArray = nullptr;
		if (Root->TryGetArrayField(TEXT("federatedElementGUIDs"), GuidsArray) && GuidsArray)
		{
			for (auto const& Val : *GuidsArray)
			{
				TSharedPtr<FJsonObject> const GuidObj = Val->AsObject();
				if (!GuidObj.IsValid())
					return false;
				double RankVal = 0;
				if (!GuidObj->TryGetNumberField(TEXT("rank"), RankVal))
					return false;
				FString GuidStr;
				if (!GuidObj->TryGetStringField(TEXT("guid"), GuidStr))
					return false;
				FGuid Guid;
				if (!FGuid::Parse(GuidStr, Guid))
					return false;
				FElemGuid Entry;
				Entry.Rank = ITwinScene::ElemIdx((uint64)RankVal);
				Entry.FederatedGuid = Guid;
				FederatedElementGUIDs.insert(std::move(Entry));
			}
		}
	}

	// Deserialize DuplicateElements
	{
		TArray<TSharedPtr<FJsonValue>> const* DuplArray = nullptr;
		if (Root->TryGetArrayField(TEXT("duplicateElements"), DuplArray) && DuplArray)
		{
			DuplicateElements.clear();
			DuplicateElements.reserve(DuplArray->Num());
			for (auto const& Val : *DuplArray)
			{
				TArray<TSharedPtr<FJsonValue>> const* IndicesArr = nullptr;
				// Val is a JSON array
				auto const& InnerArr = Val->AsArray();
				FDuplicateElementsVec DuplVec;
				for (auto const& IdxVal : InnerArr)
				{
					DuplVec.push_back(ITwinScene::ElemIdx((uint64)IdxVal->AsNumber()));
				}
				DuplicateElements.push_back(std::move(DuplVec));
			}
		}
	}

	// Deserialize ConstructionDetailingParentsToHide
	{
		TArray<TSharedPtr<FJsonValue>> const* ConstrArr = nullptr;
		if (Root->TryGetArrayField(TEXT("constructionDetailingParentsToHide"), ConstrArr) && ConstrArr)
		{
			ConstructionDetailingParentsToHide.clear();
			ConstructionDetailingParentsToHide.reserve(ConstrArr->Num());
			for (auto const& Val : *ConstrArr)
			{
				ConstructionDetailingParentsToHide.push_back(ITwinScene::ElemIdx((uint64)Val->AsNumber()));
			}
		}
	}

	return true;
}