
#include "CL_Hologram.h"
#include "CL_CounterLimiter.h"
#include "Buildables/FGBuildableConveyorBelt.h"

//#pragma optimize("", off)
void ACL_Hologram::SetHologramLocationAndRotation(const FHitResult& hitResult)
{
        AActor* hitActor = hitResult.GetActor();
        ACL_CounterLimiter* cl = Cast <ACL_CounterLimiter>(hitActor);
        if (hitActor && cl)
        {
            FRotator addedRotation = FRotator(0, 0, 0);
            if (this->GetRotationStep() != 0)
            {
                addedRotation.Yaw = this->GetScrollRotateValue();
            }
            if (hitResult.ImpactNormal == FVector(0, 0, -1))
            {
                SetActorLocationAndRotation(cl->GetActorLocation() + FVector(0, 0, -200), cl->GetActorRotation() + addedRotation);
                this->mSnappedBuilding = cl;
                this->CheckValidPlacement();
            }
            else if (hitResult.ImpactNormal == FVector(0, 0, 1))
            {
                SetActorLocationAndRotation(cl->GetActorLocation() + FVector(0, 0, 200), cl->GetActorRotation() + addedRotation);
                this->mSnappedBuilding = cl;
                this->CheckValidPlacement();
            }
            else
            {
                Super::SetHologramLocationAndRotation(hitResult);
            }
        }
    else 
        {
        Super::SetHologramLocationAndRotation(hitResult);
    }
}
//#pragma optimize("", on)

//#pragma optimize("", off)
void ACL_Hologram::ConfigureComponents(AFGBuildable* inBuildable) const
{
	Super::ConfigureComponents(inBuildable);

	if (mSnappedConveyor && inBuildable)
	{
		auto cl = Cast< ACL_CounterLimiter>(inBuildable);
		if (cl)
		{
			TArray< AFGBuildableConveyorBelt* > Belts = AFGBuildableConveyorBelt::Split(mSnappedConveyor, mSnappedConveyorOffset, false);
			for (auto Belt : Belts)
			{
                if (!Belt->GetConnection0()->IsConnected())
                {
                    if (Belt->GetConnection0()->GetDirection() == EFactoryConnectionDirection::FCD_INPUT)
                    {
                        Belt->GetConnection0()->SetConnection(cl->outputConnection);
                    }
                    else if (Belt->GetConnection0()->GetDirection() == EFactoryConnectionDirection::FCD_OUTPUT)
                    {
                        Belt->GetConnection0()->SetConnection(cl->inputConnection);
                    }
                }
                else if (!Belt->GetConnection1()->IsConnected())
                {
                    if (Belt->GetConnection1()->GetDirection() == EFactoryConnectionDirection::FCD_INPUT)
                    {
                        Belt->GetConnection1()->SetConnection(cl->outputConnection);
                    }
                    else if (Belt->GetConnection1()->GetDirection() == EFactoryConnectionDirection::FCD_OUTPUT)
                    {
                        Belt->GetConnection1()->SetConnection(cl->inputConnection);
                    }
                }
			}
		}
	}
}
//#pragma optimize("", on)