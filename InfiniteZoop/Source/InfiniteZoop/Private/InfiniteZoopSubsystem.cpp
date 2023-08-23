#include "InfiniteZoopSubsystem.h"

AInfiniteZoopSubsystem::AInfiniteZoopSubsystem()
{
    ReplicationPolicy = ESubsystemReplicationPolicy::SpawnOnServer_Replicate;
	csection = new FCriticalSection();
}

void AInfiniteZoopSubsystem::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AInfiniteZoopSubsystem, currentZoopAmount);
}

void AInfiniteZoopSubsystem::SetPublicZoopAmount(int x, int y, int z, bool foundation, bool verticalZoop, APawn* owner, FCriticalSection* lockObj)
{
	if (lockObj->TryLock())
	{
		if (!owner)
		{
			return;
		}
		FZoopAmountStruct zStruct;
		if (ZoopAmountStructs.Num() > 0 && ZoopAmountStructs.Contains(owner))
		{
			zStruct = ZoopAmountStructs[owner];
		}
		zStruct.isFoundation = foundation;
		if (foundation && verticalZoop)
		{
			zStruct.xAmount = -1;
			zStruct.zAmount = -1;
			zStruct.yAmount = -1;
			zStruct.needsUpdate = true;
			ZoopAmountStructs.Add(owner, zStruct);
			return;
		}
		if (x == xAmount && y == yAmount && z == zAmount && foundation == isFoundation)
		{
			zStruct.xAmount = -1;
			zStruct.zAmount = -1;
			zStruct.yAmount = -1;
			zStruct.isFoundation = foundation;
			needsUpdate = false;
		}
		else
		{
			zStruct.xAmount = x;
			zStruct.yAmount = y;
			zStruct.zAmount = z;
			zStruct.isFoundation = foundation;
			zStruct.needsUpdate = true;
		}
		ZoopAmountStructs.Add(owner, zStruct);
		lockObj->Unlock();
	}
}
