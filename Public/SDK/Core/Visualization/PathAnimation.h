/*--------------------------------------------------------------------------------------+
|
|     $Source: PathAnimation.h $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once

#include <functional>
#include <string>
#include <set>
#include "Core/Network/Network.h"
#include "Core/Tools/Tools.h"
#include <Core/Visualization/SavableItem.h>

namespace AdvViz::SDK
{
	class IInstance;
	class IInstancesManager;
	class IInstancesGroup;
	class ISplinesManager;
	class Http;

	using namespace Tools;

	class IAnimationPathInfo : public Tools::Factory<IAnimationPathInfo>
		//, public WithStrongTypeId<IAnimationPathInfo>
		, public Tools::ExtensionSupport
		, public ISavableItem
	{
	public:
		struct SPathAnimationInfo
		{
			std::optional<std::string> id; // animation id defined by the server
			std::optional<std::string> splineId; // spline id defined by the server
			std::optional<double> speed;
			std::optional<double> offsetX; // obsolete
			std::optional<double> offsetY; // obsolete
			std::optional<double> startTime; // = animation start delay
			std::optional<bool> hasLoop;
			std::optional<bool> isEnabled;
			std::optional<bool> invDir;
			std::optional<int> repeatMode;
			std::optional<bool> oneWay;
			std::optional<int> laneCount;
			std::optional<double> laneWidth;
			std::optional<double> density;
			std::optional<double> sepWidth;
			std::optional<double> minSpeed;
			std::optional<double> maxSpeed;
			std::optional<std::vector<std::string>> objects;
			std::optional<std::string> instGroupId; // instance group id defined by the server (to be removed as we can always retrieve a group using spline id)
		};

		virtual const RefID& GetSplineId() const = 0;
		virtual void SetSplineId(const RefID& id) = 0;

		//virtual const RefID& GetInstGroupId() const = 0;
		//virtual void SetInstGroupId(const RefID& id) = 0;

		virtual void SetSpeed(double v) = 0;
		virtual double GetSpeed() const = 0;

		virtual void SetOffsetX(double v) = 0;
		virtual double GetOffsetX() const = 0;

		virtual void SetOffsetY(double v) = 0;
		virtual double GetOffsetY() const = 0;

		virtual void SetStartTime(double v) = 0;
		virtual double GetStartTime() const = 0;

		virtual void SetIsLooping(bool b) = 0;
		virtual bool IsLooping() const = 0;

		virtual void SetIsEnabled(bool b) = 0;
		virtual bool IsEnabled() const = 0;

		virtual void SetInvDir(bool b) = 0;
		virtual bool HasInvDir() const = 0;
		
		virtual void SetRepeatMode(int b) = 0;
		virtual int GetRepeatMode() const = 0;
		
		virtual void SetOneWay(bool b) = 0;
		virtual bool IsOneWay() const = 0;
		
		virtual void SetLaneCount(int b) = 0;
		virtual int GetLaneCount() const = 0;

		virtual void SetLaneWidth(double v) = 0;
		virtual double GetLaneWidth() const = 0;

		virtual void SetDensity(double v) = 0;
		virtual double GetDensity() const = 0;

		virtual void SetSepWidth(double v) = 0;
		virtual double GetSepWidth() const = 0;

		virtual void SetMinSpeed(double v) = 0;
		virtual double GetMinSpeed() const = 0;
		
		virtual void SetMaxSpeed(double v) = 0;
		virtual double GetMaxSpeed() const = 0;

		virtual void SetObjects(const std::vector<std::string> &objs) = 0;
		virtual void GetObjects(std::vector<std::string>& objs) const = 0;

		virtual void SetServerSideData(const SPathAnimationInfo &data) = 0;
		virtual const SPathAnimationInfo& GetServerSideData() const = 0;

		//struct TimelineValue {
		//	float3 translation;
		//	float4 quaternion;
		//};
	};

	using IAnimationPathInfoPtr = TSharedLockableDataPtr<IAnimationPathInfo>;
	using IAnimationPathInfoWPtr = TSharedLockableDataWPtr<IAnimationPathInfo>;

	class ADVVIZ_LINK AnimationPathInfo : public IAnimationPathInfo, public Tools::TypeId<AnimationPathInfo>
	{
	public:
		AnimationPathInfo();
		virtual ~AnimationPathInfo();

		//------------------------------------------------------------------------------
		 /// overridden from ISavableItem
		const RefID& GetId() const override;
		void SetId(const RefID& id) override;

		ESaveStatus GetSaveStatus() const override;
		void SetSaveStatus(ESaveStatus status) override;
		//------------------------------------------------------------------------------

		const RefID& GetSplineId() const override;
		void SetSplineId(const RefID& id) override;

		//const RefID& GetInstGroupId() const override;
		//void SetInstGroupId(const RefID& id) override;

		void SetSpeed(double v) override;
		double GetSpeed() const override;

		void SetOffsetX(double v) override;
		double GetOffsetX() const override;

		void SetOffsetY(double v) override;
		double GetOffsetY() const override;

		void SetStartTime(double v) override;
		double GetStartTime() const override;

		void SetIsLooping(bool b) override;
		bool IsLooping() const override;

		void SetIsEnabled(bool b) override;
		bool IsEnabled() const override;

		void SetInvDir(bool b) override;
		bool HasInvDir() const override;

		void SetRepeatMode(int b) override;
		int GetRepeatMode() const override;

		void SetOneWay(bool b) override;
		bool IsOneWay() const override;

		void SetLaneCount(int b) override;
		int GetLaneCount() const override;

		void SetLaneWidth(double v) override;
		double GetLaneWidth() const override;

		void SetDensity(double v) override;
		double GetDensity() const override;

		void SetSepWidth(double v) override;
		double GetSepWidth() const override;

		void SetMinSpeed(double v) override;
		double GetMinSpeed() const override;

		void SetMaxSpeed(double v) override;
		double GetMaxSpeed() const override;

		void SetObjects(const std::vector<std::string> &objs) override;
		void GetObjects(std::vector<std::string>& objs) const override;

		void SetServerSideData(const SPathAnimationInfo& data) override;
		const SPathAnimationInfo& GetServerSideData() const override;

		using Tools::TypeId<AnimationPathInfo>::GetTypeId;
		std::uint64_t GetDynTypeId() const override { return GetTypeId(); }
		bool IsTypeOf(std::uint64_t i) const override { return (i == GetTypeId()) || IAnimationPathInfo::IsTypeOf(i); }

		class Impl;
		Impl& GetImpl();
		const Impl& GetImpl() const;

	private:
		const std::shared_ptr<Impl> impl_;
	};

	class IPathAnimManager : public Tools::Factory<IPathAnimManager>// : public Tools::CommonInterfaceClass<IPathAnimManager>
	{
	public:
		virtual void SetInstanceManager(const std::shared_ptr<IInstancesManager>& instanceManager) = 0;
		virtual void SetSplinesManager(const std::shared_ptr<ISplinesManager>& splinesManager) = 0;

		virtual size_t GetNumberOfPaths() const = 0;
		virtual	IAnimationPathInfoPtr FindAnimationPathInfoByDBId(const std::string& id) const = 0;
		virtual IAnimationPathInfoPtr FindAnimationPathInfoBySplineRefId(const RefID& id) const = 0;
		virtual IAnimationPathInfoPtr AddAnimationPathInfo() = 0;
		virtual void RemoveAnimationPathInfo(const RefID& id) = 0;
		virtual IAnimationPathInfoPtr GetAnimationPathInfo(const RefID& id) const = 0;
		virtual void GetAnimationPathIds(std::set<AdvViz::SDK::RefID> &ids) const = 0;

		virtual void LoadDataFromServer(const std::string& decorationId) = 0;
		virtual void AsyncLoadDataFromServer(const std::string& decorationId,
			const std::function<void(IAnimationPathInfoPtr&)>& onPathLoaded,
			const std::function<void(expected<void, std::string> const&)>& onComplete) = 0;
		virtual void AsyncSaveDataOnServer(const std::string& decorationId, std::function<void(bool)>&& onDataSavedFunc) = 0;

		virtual bool HasAnimPathsToSave() const = 0;
	};
	
	typedef std::shared_ptr<IPathAnimManager> IPathAnimManagerPtr;

	class ADVVIZ_LINK PathAnimManager : public IPathAnimManager, public Tools::TypeId<PathAnimManager>
	{
	public:
		PathAnimManager();

		void SetInstanceManager(const std::shared_ptr<IInstancesManager>& instanceManager) override;
		void SetSplinesManager(const std::shared_ptr<ISplinesManager>& splinesManager) override;

		size_t GetNumberOfPaths() const override;
		IAnimationPathInfoPtr FindAnimationPathInfoByDBId(const std::string& id) const override;
		IAnimationPathInfoPtr FindAnimationPathInfoBySplineRefId(const RefID& id) const override;
		IAnimationPathInfoPtr AddAnimationPathInfo() override;
		void RemoveAnimationPathInfo(const RefID& id) override;
		IAnimationPathInfoPtr GetAnimationPathInfo(const RefID& id) const override;
		void GetAnimationPathIds(std::set<AdvViz::SDK::RefID>& ids) const override;

		void LoadDataFromServer(const std::string& decorationId) override;
		void AsyncLoadDataFromServer(const std::string& decorationId,
			const std::function<void(IAnimationPathInfoPtr&)>& onPathLoaded,
			const std::function<void(expected<void, std::string> const&)>& onComplete) override;
		void AsyncSaveDataOnServer(const std::string& decorationId, std::function<void(bool)>&& onDataSavedFunc) override;

		bool HasAnimPathsToSave() const override;

		using Tools::TypeId<PathAnimManager>::GetTypeId;
		std::uint64_t GetDynTypeId() const override { return GetTypeId(); }
		bool IsTypeOf(std::uint64_t i) const override { return (i == GetTypeId()) || IPathAnimManager::IsTypeOf(i); }

		class Impl;
		Impl& GetImpl();
		const Impl& GetImpl() const;
	private:
		const std::shared_ptr<Impl> impl_;
	};
}
