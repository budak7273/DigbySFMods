


#include "LBBuild_ModularLoadBalancer.h"

#include "FGColoredInstanceMeshProxy.h"
#include "FGFactoryConnectionComponent.h"
#include "FGInventoryLibrary.h"
#include "Kismet/KismetMathLibrary.h"
#include "FGOutlineComponent.h"
#include "LBDefaultRCO.h"
#include "LoadBalancersModule.h"

DEFINE_LOG_CATEGORY(LoadBalancers_Log);
ALBBuild_ModularLoadBalancer::ALBBuild_ModularLoadBalancer()
{
    this->mInventorySizeX = 2;
    this->mInventorySizeY = 2;
    mOutputInventory = CreateDefaultSubobject<UFGInventoryComponent>(TEXT("OutputInventory"));
}

void ALBBuild_ModularLoadBalancer::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(ALBBuild_ModularLoadBalancer, mGroupModules);
    DOREPLIFETIME(ALBBuild_ModularLoadBalancer, GroupLeader);
    DOREPLIFETIME(ALBBuild_ModularLoadBalancer, mFilteredItem);
}

void ALBBuild_ModularLoadBalancer::BeginPlay()
{
    Super::BeginPlay();

    if(HasAuthority())
    {
        if(!mOutputInventory)
            mOutputInventory = UFGInventoryLibrary::CreateInventoryComponent(this, TEXT("OutputInventory"));

        if(GetBufferInventory() && MyOutputConnection)
        {
            MyOutputConnection->SetInventory(GetBufferInventory());
            MyOutputConnection->SetInventoryAccessIndex(-1);
            GetBufferInventory()->Resize(IsOverflowLoader() ? 5 : 1);
            for(int i = 0; i < GetBufferInventory()->GetSizeLinear(); ++i)
                if(GetBufferInventory()->IsValidIndex(i))
                    GetBufferInventory()->AddArbitrarySlotSize(i, 10);
            
            if(IsFilterLoader())
                GetBufferInventory()->SetAllowedItemOnIndex(0, mFilteredItem);
        }

        if(mOutputInventory)
        {
            mOutputInventory->Resize(10);
            for(int i = 0; i < mOutputInventory->GetSizeLinear(); ++i)
                if(mOutputInventory->IsValidIndex(i))
                    mOutputInventory->AddArbitrarySlotSize(i, 10);
        }
        
        ApplyGroupModule();
    }

}

bool ALBBuild_ModularLoadBalancer::IsMarkToFilter() const
{
    FInventoryStack Stack;
    if(mOutputInventory)
        mOutputInventory->GetStackFromIndex(mOutputInventory->GetFirstIndexWithItem(), Stack);

    if(Stack.HasItems())
        for (const UFGFactoryConnectionComponent* Connection : GetConnections(EFactoryConnectionDirection::FCD_OUTPUT, true))
        {
            if(Connection)
                if(const UFGInventoryComponent* InventoryComponent = Connection->GetInventory()){
                   if(InventoryComponent->IsItemAllowed(Stack.Item.ItemClass, 0))
                       return true;
                }
        }
    return false;
}

void ALBBuild_ModularLoadBalancer::SetFilteredItem(TSubclassOf<UFGItemDescriptor> ItemClass)
{
    if(HasAuthority())
    {
        if(IsFilterLoader() && GetBufferInventory())
        {
            GetBufferInventory()->SetAllowedItemOnIndex(0, ItemClass);
            mFilteredItem = GetBufferInventory()->GetAllowedItemOnIndex(0);
        }
    }
    else if(ULBDefaultRCO* RCO = ULBDefaultRCO::Get(GetWorld()))
            RCO->Server_SetFilteredItem(this, ItemClass);
}

void ALBBuild_ModularLoadBalancer::ApplyLeader()
{
    if(HasAuthority())
    {
        GroupLeader = this;

        for (ALBBuild_ModularLoadBalancer* ModularLoadBalancer : GetGroupModules())
            if(ModularLoadBalancer)
            {
                ModularLoadBalancer->GroupLeader = GroupLeader;
                ModularLoadBalancer->ForceNetUpdate();
            }

        UpdateCache();
        ForceNetUpdate();
    }
}

TArray<ALBBuild_ModularLoadBalancer*> ALBBuild_ModularLoadBalancer::GetGroupModules() const
{
    if(!GroupLeader)
        return {};
    
    TArray<ALBBuild_ModularLoadBalancer*> Return;
    TArray<TWeakObjectPtr<ALBBuild_ModularLoadBalancer>> Array = GroupLeader->mGroupModules;

    if(Array.Num() > 0)
        for (TWeakObjectPtr<ALBBuild_ModularLoadBalancer> ModularLoadBalancer : Array)
            if(ModularLoadBalancer.IsValid())
                Return.Add(ModularLoadBalancer.Get());
    return Return;
}

void ALBBuild_ModularLoadBalancer::Factory_Tick(float dt)
{
    Super::Factory_Tick(dt);

    if(IsLeader() && HasAuthority())
    {
        mTimer += dt;
        if(mTimer >= 1.f)
            UpdateCache();

        if(IsFilterLoader() && GetBufferInventory())
            if(mFilteredItem != GetBufferInventory()->GetAllowedItemOnIndex(0))
                GetBufferInventory()->SetAllowedItemOnIndex(0, mFilteredItem);

        if(mOutputInventory)
            if(!mOutputInventory->IsEmpty())
            {
                if(IsMarkToFilter())
                {
                    if(!TickGroupTypeOutput(mFilteredLoaderData, dt))
                        TickGroupTypeOutput(mNormalLoaderData, dt);
                }
                else
                    TickGroupTypeOutput(mNormalLoaderData, dt);
            }
    }
}

bool ALBBuild_ModularLoadBalancer::TickGroupTypeOutput(FLBBalancerData& TypeData, float dt)
{
    if(TypeData.mConnectedOutputs.Num() > 0 && mOutputInventory)
    {
        if(!TypeData.mConnectedOutputs.IsValidIndex(TypeData.mOutputIndex))
            TypeData.mOutputIndex = 0;

        // if still BREAK! something goes wrong here!
        if(!TypeData.mConnectedOutputs.IsValidIndex(TypeData.mOutputIndex))
        {
            UE_LOG(LoadBalancers_Log, Error, TEXT("mConnectedOutputs has still a invalid Index!"));
            return false;
        }
        if(!TypeData.mConnectedOutputs[TypeData.mOutputIndex].IsValid())
            return false;

        if(!mOutputInventory || mOutputInventory->IsEmpty())
            return false;

        ALBBuild_ModularLoadBalancer* Balancer = TypeData.mConnectedOutputs[TypeData.mOutputIndex].Get();
            
        TypeData.mOutputIndex++;
        return SendItemsToOutputs(dt, Balancer);
    }
    return false;
}

bool ALBBuild_ModularLoadBalancer::CanSendToOverflow() const
{
    bool CanSend = HasOverflowLoader();
    if(CanSend)
    {
        const int Index = mOutputInventory->GetFirstIndexWithItem();
        if(Index >= 0)
        {
            FInventoryStack Stack;
            mOutputInventory->GetStackFromIndex(Index, Stack);
            CanSend = Stack.HasItems() ? GetOverflowLoader()->GetBufferInventory()->HasEnoughSpaceForItem(Stack.Item) : false;

            if(CanSend)
                for (ALBBuild_ModularLoadBalancer* GroupModule : GetGroupModules())
                {
                    if(GroupModule->GetBufferInventory()->HasEnoughSpaceForItem(Stack.Item) && Stack.HasItems())
                    {
                        CanSend = false;
                        break;
                    }
                }
        }
    }
    return CanSend;
}

bool ALBBuild_ModularLoadBalancer::SendItemsToOutputs(float dt, ALBBuild_ModularLoadBalancer* Balancer)
{
    if(Balancer->GetBufferInventory() && mOutputInventory)
    {
        const int Index = mOutputInventory->GetFirstIndexWithItem();
        if(Index >= 0)
        {
            FInventoryStack Stack;
            mOutputInventory->GetStackFromIndex(Index, Stack);
            if(Stack.HasItems() && Balancer->GetBufferInventory()->HasEnoughSpaceForItem(Stack.Item))
            {
                Balancer->GetBufferInventory()->AddItem(Stack.Item);
                mOutputInventory->RemoveFromIndex(Index,1 );
                return true;
            }
        }
    }
    return false;
}

void ALBBuild_ModularLoadBalancer::UpdateCache()
{
    mTimer = .0f;

    TArray<TWeakObjectPtr<ALBBuild_ModularLoadBalancer>> ConnectedOutputs = {};
    TArray<TWeakObjectPtr<ALBBuild_ModularLoadBalancer>> ConnectedInputs = {};
    
    TArray<TWeakObjectPtr<ALBBuild_ModularLoadBalancer>> FilteredConnectedOutputs = {};
    TArray<TWeakObjectPtr<ALBBuild_ModularLoadBalancer>> FilteredConnectedInputs = {};
    
    for (ALBBuild_ModularLoadBalancer* GroupModule : GetGroupModules())
    {
        // We skip here the overflow Loader otherwise it push in normal cycle all there
        if(GroupModule->IsOverflowLoader())
            continue;
        
        if(GroupModule->MyInputConnection)
            if(GroupModule->MyInputConnection->IsConnected())
                if(GroupModule->IsFilterLoader())
                {
                    FilteredConnectedInputs.AddUnique(GroupModule);
                }
                else
                {
                    ConnectedInputs.AddUnique(GroupModule);
                }
                
        if(GroupModule->MyOutputConnection)
            if(GroupModule->MyOutputConnection->IsConnected())
                if(GroupModule->IsFilterLoader())
                {
                    FilteredConnectedOutputs.AddUnique(GroupModule);
                }
                else
                {
                    ConnectedOutputs.AddUnique(GroupModule);
                }
    }

    mNormalLoaderData.mConnectedInputs = ConnectedInputs;
    mNormalLoaderData.mConnectedOutputs = ConnectedOutputs;

    mFilteredLoaderData.mConnectedInputs = FilteredConnectedInputs;
    mFilteredLoaderData.mConnectedOutputs = FilteredConnectedOutputs;
}

void ALBBuild_ModularLoadBalancer::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    // Apply new Leader to the group if the group has more as 1 module (single loader)
    // EndPlay is here a better use. We should do this better only if it's Destroyed on EndPlay (is also in no case to late to can be invalid)
    if(EndPlayReason == EEndPlayReason::Destroyed)
        if (IsLeader())
            if (GetGroupModules().Num() > 1)
                for (ALBBuild_ModularLoadBalancer* LoadBalancer : GetGroupModules())
                    if(LoadBalancer != this)
                    {
                        LoadBalancer->ApplyLeader();
                        break;
                    }
    
   Super::EndPlay(EndPlayReason);
}

void ALBBuild_ModularLoadBalancer::ApplyGroupModule()
{
    if(HasAuthority() && GroupLeader)
    {
        GroupLeader->mGroupModules.AddUnique(this);
        if(IsOverflowLoader())
        {
            GroupLeader->mOverflowLoader = this;
        }

        UpdateCache();
        
        ForceNetUpdate();
    }
    else
        UE_LOG(LoadBalancers_Log, Error, TEXT("Cannot Apply GroupLeader: Invalid"))
}

TArray<UFGFactoryConnectionComponent*> ALBBuild_ModularLoadBalancer::GetConnections(
    EFactoryConnectionDirection Direction, bool Filtered) const
{
    if(!GroupLeader)
        return {};

    FLBBalancerData Data = Filtered ? GroupLeader->mFilteredLoaderData : GroupLeader->mNormalLoaderData;
    
    TArray<UFGFactoryConnectionComponent*> Return;
    TArray<TWeakObjectPtr<ALBBuild_ModularLoadBalancer>> Array = Direction == EFactoryConnectionDirection::FCD_INPUT ? Data.mConnectedInputs : Data.mConnectedOutputs;;
    
    for (TWeakObjectPtr<ALBBuild_ModularLoadBalancer> LoadBalancer : Array)
        if(LoadBalancer.IsValid())
            Return.Add(Direction == EFactoryConnectionDirection::FCD_INPUT ? LoadBalancer->MyInputConnection : LoadBalancer->MyOutputConnection);
    
    return Return;
}

void ALBBuild_ModularLoadBalancer::PostInitializeComponents()
{
    Super::PostInitializeComponents();

    for (UActorComponent* ComponentsByClass : GetComponentsByClass(UFGFactoryConnectionComponent::StaticClass()))
        if(UFGFactoryConnectionComponent* ConnectionComponent = Cast<UFGFactoryConnectionComponent>(ComponentsByClass))
        {
            if(ConnectionComponent->GetDirection() == EFactoryConnectionDirection::FCD_INPUT)
            {
                MyInputConnection = ConnectionComponent;
            }
            else if(ConnectionComponent->GetDirection() == EFactoryConnectionDirection::FCD_OUTPUT)
            {
                MyOutputConnection = ConnectionComponent;
            }
        }
}

void ALBBuild_ModularLoadBalancer::Factory_CollectInput_Implementation()
{
    Super::Factory_CollectInput_Implementation();
    
    if(!IsLeader() || !HasAuthority())
        return;

    if(mNormalLoaderData.mConnectedInputs.Num() == 0)
        return;

    if(!mNormalLoaderData.mConnectedInputs.IsValidIndex(mNormalLoaderData.mInputIndex))
        mNormalLoaderData.mInputIndex = 0;

    // if still BREAK! something goes wrong here!
    if(!mNormalLoaderData.mConnectedInputs.IsValidIndex(mNormalLoaderData.mInputIndex))
    {
        UE_LOG(LoadBalancers_Log, Error, TEXT("mConnectedInputs has still a invalid Index!"));
        return;
    }

    CollectInput(mNormalLoaderData.mConnectedInputs[mNormalLoaderData.mInputIndex].Get());
    mNormalLoaderData.mInputIndex++;
}

void ALBBuild_ModularLoadBalancer::CollectInput(ALBBuild_ModularLoadBalancer* Module)
{
    UFGFactoryConnectionComponent* connection = Module->MyInputConnection;
    if(!connection || !GroupLeader)
        return;

    if(connection->IsConnected() && GroupLeader->mOutputInventory)
    {
        TArray<FInventoryItem> peeker;
        if (connection->Factory_PeekOutput(peeker))
        {
            if(connection->GetInventory() != GroupLeader->mOutputInventory)
                connection->SetInventory(GroupLeader->mOutputInventory);

            
            FInventoryItem Item = peeker[0];
            float offset;

            if(GroupLeader->mOutputInventory->GetNumItems(Item.ItemClass) <= 10)
            {
                if(connection->Factory_GrabOutput(Item, offset))
                {
                    GroupLeader->mOutputInventory->AddItem(Item);
                    return;
                }
            }
            
            if(HasOverflowLoader())
            {
                ALBBuild_ModularLoadBalancer* OverflowLoader = GetOverflowLoader();
                if(OverflowLoader->GetBufferInventory())
                    if(OverflowLoader->GetBufferInventory()->HasEnoughSpaceForItem(Item))
                        if(connection->Factory_GrabOutput(Item, offset))
                            OverflowLoader->GetBufferInventory()->AddItem(Item);
            }
        }
    }
}
