#include "FlowFieldTargetComponent.h"
#include "FlowFieldSubsystem.h"
#include "FlowFieldActor.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"

UFlowFieldTargetComponent::UFlowFieldTargetComponent()
{
    PrimaryComponentTick.bCanEverTick = true;
}

void UFlowFieldTargetComponent::BeginPlay()
{
    Super::BeginPlay();

    // 缓存原始 MaxWalkSpeed，供速度乘数运算使用
    if (ACharacter* Char = Cast<ACharacter>(GetOwner()))
    {
        if (UCharacterMovementComponent* Move = Char->GetCharacterMovement())
        {
            BaseWalkSpeed = Move->MaxWalkSpeed;
        }
    }

    UWorld* World = GetWorld();
    if (!World) return;

    UFlowFieldSubsystem* Sub = World->GetSubsystem<UFlowFieldSubsystem>();
    AFlowFieldActor* Actor = Sub ? Sub->GetActor() : nullptr;
    if (Actor)
        Actor->RegisterTarget(this);
}

void UFlowFieldTargetComponent::TickComponent(float DeltaTime, ELevelTick TickType,
    FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

    // 将人群减速乘数实时应用到角色移动速度
    if (BaseWalkSpeed > 0.f)
    {
        if (ACharacter* Char = Cast<ACharacter>(GetOwner()))
        {
            if (UCharacterMovementComponent* Move = Char->GetCharacterMovement())
            {
                Move->MaxWalkSpeed = BaseWalkSpeed * CurrentSpeedMultiplier;
            }
        }
    }
}

void UFlowFieldTargetComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    // 恢复原始速度，避免组件卸载后角色一直保持减速状态
    if (BaseWalkSpeed > 0.f)
    {
        if (ACharacter* Char = Cast<ACharacter>(GetOwner()))
        {
            if (UCharacterMovementComponent* Move = Char->GetCharacterMovement())
            {
                Move->MaxWalkSpeed = BaseWalkSpeed;
            }
        }
    }

    UWorld* World = GetWorld();
    if (World)
    {
        UFlowFieldSubsystem* Sub = World->GetSubsystem<UFlowFieldSubsystem>();
        AFlowFieldActor* Actor = Sub ? Sub->GetActor() : nullptr;
        if (Actor)
            Actor->UnregisterTarget(this);
    }

    Super::EndPlay(EndPlayReason);
}
