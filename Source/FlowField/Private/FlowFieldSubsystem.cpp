#include "FlowFieldSubsystem.h"
#include "FlowFieldActor.h"
#include "FlowFieldObstacleActor.h"
#include "DrawDebugHelpers.h"
#include "EngineUtils.h"
#include "FlowFieldSettings.h"
#include "MassCommandBuffer.h"
#include "MassCommands.h"
#include "MassCommonFragments.h"
#include "MassExecutionContext.h"
#include "MassReplicationSubsystem.h"
#include "MassAI/FlowFieldAgentFragment.h"
#include "MassReplication/FlowFieldAgentReplicator.h"

// ── PostInitialize ────────────────────────────────────────────────

TSubclassOf<AMassClientBubbleInfoBase> UFlowFieldSubsystem::GetBubbleInfoClass() const
{
	const UFlowFieldSettings* Settings = UFlowFieldSettings::Get();

	// 先打印配置里读到的原始字符串，确认 ini 写入生效
	UE_LOG(LogTemp, Log, TEXT("[FlowField] ClientBubbleInfoClass config = %s"),
	       *Settings->ClientBubbleInfoClass.ToString());

	TSubclassOf<AMassClientBubbleInfoBase> Configured =
		Settings->GetResolvedBubbleInfoClass();

	UE_LOG(LogTemp, Log, TEXT("[FlowField] Resolved BubbleInfoClass = %s"),
	       Configured ? *Configured->GetName() : TEXT("NULL"));

	if (Configured)
		return Configured;
	return AFlowFieldClientBubbleInfo::StaticClass();
}

void UFlowFieldSubsystem::PostInitialize()
{
	Super::PostInitialize();

	UWorld* World = GetWorld();
	if (!World || !World->IsGameWorld()) return;

	TSubclassOf<AMassClientBubbleInfoBase> BubbleClass = GetBubbleInfoClass();
	if (!BubbleClass) return;

	UMassReplicationSubsystem* RepSub =
		UWorld::GetSubsystem<UMassReplicationSubsystem>(World);
	if (!RepSub)
	{
		UE_LOG(LogTemp, Error, TEXT("[FlowFieldSubsystem] 获取 MassReplicationSubsystem 失败"));
		return;
	}

	RepSub->RegisterBubbleInfoClass(BubbleClass);
	UE_LOG(LogTemp, Log, TEXT("[FlowFieldSubsystem] RegisterBubbleInfoClass: %s"),
	       *BubbleClass->GetName());

	// Spawn 由项目层 Subsystem 负责（如 MonsterManager）
	// 插件只注册，不 Spawn
}

// ── RegisterActor ─────────────────────────────────────────────────

void UFlowFieldSubsystem::RegisterActor(AFlowFieldActor* Actor)
{
	FlowFieldActor = Actor;
	UE_LOG(LogTemp, Log, TEXT("[FlowFieldSubsystem] Actor registered: %s"), *GetNameSafe(Actor));
}

// ── Tick ──────────────────────────────────────────────────────────

void UFlowFieldSubsystem::Tick(float DeltaTime)
{
	if (!ScanState.IsValid()) return;

	if (!ScanState->bSubmitDone)
		SubmitNextBatch();
}

// ── Query ─────────────────────────────────────────────────────────

FVector UFlowFieldSubsystem::GetFlowDirection(FVector WorldPos) const
{
	if (!FlowFieldActor) return FVector::ZeroVector;
	return FlowFieldActor->GetFlowDirection(WorldPos);
}

FVector UFlowFieldSubsystem::GetFlowDirectionSmooth(FVector WorldPos) const
{
	if (!FlowFieldActor) return FVector::ZeroVector;
	return FlowFieldActor->GetFlowDirectionSmooth(WorldPos);
}

bool UFlowFieldSubsystem::CanReach(FVector WorldPos) const
{
	if (!FlowFieldActor) return false;
	return FlowFieldActor->CanReach(WorldPos);
}

float UFlowFieldSubsystem::GetIntegration(FVector WorldPos) const
{
	if (!FlowFieldActor) return -1.f;
	return FlowFieldActor->GetIntegration(WorldPos);
}

void UFlowFieldSubsystem::Generate(FVector GoalPos)
{
	if (!FlowFieldActor)
	{
		UE_LOG(LogTemp, Error, TEXT("[FlowFieldSubsystem] Generate: no Actor registered"));
		return;
	}
	FlowFieldActor->Generate(GoalPos);
}

bool UFlowFieldSubsystem::IsReady() const
{
	return FlowFieldActor && FlowFieldActor->bReady;
}

// ── 攻击系统转发 ──────────────────────────────────────────────────

#define ATTACK_COMP (FlowFieldActor && FlowFieldActor->AttackComp)

int32 UFlowFieldSubsystem::FireProjectile(const FFlowFieldProjectileConfig& Config)
{ return ATTACK_COMP ? FlowFieldActor->AttackComp->FireProjectile(Config) : -1; }

int32 UFlowFieldSubsystem::FireLaser(const FFlowFieldLaserConfig& Config)
{ return ATTACK_COMP ? FlowFieldActor->AttackComp->FireLaser(Config) : -1; }

int32 UFlowFieldSubsystem::FireChain(const FFlowFieldChainConfig& Config)
{ return ATTACK_COMP ? FlowFieldActor->AttackComp->FireChain(Config) : -1; }

int32 UFlowFieldSubsystem::FireExplosion(const FFlowFieldExplosionConfig& Config)
{ return ATTACK_COMP ? FlowFieldActor->AttackComp->FireExplosion(Config) : -1; }

void UFlowFieldSubsystem::CancelAttack(int32 AttackId)
{ if (ATTACK_COMP) FlowFieldActor->AttackComp->CancelAttack(AttackId); }

void UFlowFieldSubsystem::KillAgent(int32 EntityId)
{ if (ATTACK_COMP) FlowFieldActor->AttackComp->KillAgent(EntityId); }

void UFlowFieldSubsystem::DestroyAgent(int32 EntityId)
{ if (ATTACK_COMP) FlowFieldActor->AttackComp->DestroyAgent(EntityId); }

#undef ATTACK_COMP

// ── 击退 ──────────────────────────────────────────────────────────

void UFlowFieldSubsystem::ApplyKnockback(FVector WorldPos, float Radius,
                                         FVector Direction, float Force,
                                         float DecaySpeed)
{
	UWorld* World = GetWorld();
	if (!World || World->GetNetMode() == NM_Client) return;

	FMassEntityManager* EM = UE::Mass::Utils::GetEntityManager(World);
	if (!EM) return;

	Direction.Z = 0.f;
	Direction.Normalize();
	if (Direction.IsNearlyZero()) return;

	if (!KnockbackQuery.IsInitialized())
	{
		KnockbackQuery.Initialize(EM->AsShared());
		KnockbackQuery.AddRequirement<FFlowFieldAgentFragment>(EMassFragmentAccess::ReadWrite);
		KnockbackQuery.AddRequirement<FTransformFragment>(EMassFragmentAccess::ReadOnly);
		KnockbackQuery.AddTagRequirement<FFlowFieldAgentTag>(EMassFragmentPresence::All);
	}

	const float RadiusSq = Radius * Radius;
	const FVector KBVelocity = Direction * Force;

	TArray<TPair<FMassEntityHandle, float>> Targets;
	FMassExecutionContext Context(*EM, 0.f);
	KnockbackQuery.ForEachEntityChunk(Context,
	                                  [&](FMassExecutionContext& Chunk)
	                                  {
		                                  auto Transforms = Chunk.GetFragmentView<FTransformFragment>();
		                                  const int32 Num = Chunk.GetNumEntities();
		                                  for (int32 i = 0; i < Num; i++)
		                                  {
			                                  float DistSq = FVector::DistSquared2D(
				                                  Transforms[i].GetTransform().GetLocation(), WorldPos);
			                                  if (DistSq > RadiusSq) continue;
			                                  float DistRatio = 1.f - FMath::Sqrt(DistSq) / FMath::Sqrt(RadiusSq);
			                                  Targets.Add({Chunk.GetEntity(i), DistRatio});
		                                  }
	                                  });

	if (Targets.IsEmpty()) return;

	EM->Defer().PushCommand<FMassDeferredSetCommand>(
		[Targets, KBVelocity, DecaySpeed](FMassEntityManager& Manager)
		{
			for (const auto& T : Targets)
			{
				if (!Manager.IsEntityActive(T.Key)) continue;
				FMassEntityView View(Manager, T.Key);
				FFlowFieldAgentFragment* Agent = View.GetFragmentDataPtr<FFlowFieldAgentFragment>();
				if (!Agent) continue;
				Agent->KnockbackVelocity = KBVelocity * T.Value;
				Agent->KnockbackDecay = DecaySpeed;
				Agent->bIsKnockedBack = true;
			}
		});
}

void UFlowFieldSubsystem::ApplyExplosionKnockback(FVector WorldPos, float Radius,
                                                  float Force, float DecaySpeed)
{
	UWorld* World = GetWorld();
	if (!World || World->GetNetMode() == NM_Client) return;

	FMassEntityManager* EM = UE::Mass::Utils::GetEntityManager(World);
	if (!EM) return;

	if (!KnockbackQuery.IsInitialized())
	{
		KnockbackQuery.Initialize(EM->AsShared());
		KnockbackQuery.AddRequirement<FFlowFieldAgentFragment>(EMassFragmentAccess::ReadWrite);
		KnockbackQuery.AddRequirement<FTransformFragment>(EMassFragmentAccess::ReadOnly);
		KnockbackQuery.AddTagRequirement<FFlowFieldAgentTag>(EMassFragmentPresence::All);
	}

	const float RadiusSq = Radius * Radius;

	struct FKBTarget
	{
		FMassEntityHandle Handle;
		FVector RadialDir;
		float DistRatio;
	};
	TArray<FKBTarget> Targets;

	FMassExecutionContext Context(*EM, 0.f);
	KnockbackQuery.ForEachEntityChunk(Context,
	                                  [&](FMassExecutionContext& Chunk)
	                                  {
		                                  auto Transforms = Chunk.GetFragmentView<FTransformFragment>();
		                                  const int32 Num = Chunk.GetNumEntities();
		                                  for (int32 i = 0; i < Num; i++)
		                                  {
			                                  FVector RadialDir = Transforms[i].GetTransform().GetLocation() - WorldPos;
			                                  RadialDir.Z = 0.f;
			                                  float Dist = RadialDir.Size2D();
			                                  if (Dist * Dist > RadiusSq) continue;

			                                  if (Dist < KINDA_SMALL_NUMBER)
			                                  {
				                                  float Angle = FMath::FRand() * 2.f * PI;
				                                  RadialDir = FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0.f);
				                                  Dist = 1.f;
			                                  }
			                                  RadialDir /= Dist;

			                                  float DistRatio = 1.f - (Dist * Dist) / RadiusSq;
			                                  Targets.Add({
				                                  Chunk.GetEntity(i), RadialDir, FMath::Clamp(DistRatio, 0.f, 1.f)
			                                  });
		                                  }
	                                  });

	if (Targets.IsEmpty()) return;

	EM->Defer().PushCommand<FMassDeferredSetCommand>(
		[Targets, Force, DecaySpeed](FMassEntityManager& Manager)
		{
			for (const FKBTarget& T : Targets)
			{
				if (!Manager.IsEntityActive(T.Handle)) continue;
				FMassEntityView View(Manager, T.Handle);
				FFlowFieldAgentFragment* Agent = View.GetFragmentDataPtr<FFlowFieldAgentFragment>();
				if (!Agent) continue;
				Agent->KnockbackVelocity = T.RadialDir * Force * T.DistRatio;
				Agent->KnockbackDecay = DecaySpeed;
				Agent->bIsKnockedBack = true;
			}
		});
}

// ── ScanAndPlaceObstacles ─────────────────────────────────────────

void UFlowFieldSubsystem::ScanAndPlaceObstacles(int32 BatchSize)
{
	if (ScanState.IsValid())
	{
		UE_LOG(LogTemp, Warning, TEXT("[FlowFieldSubsystem] Scan already in progress"));
		return;
	}

	UWorld* World = GetWorld();
	if (!World) return;

	if (!FlowFieldActor)
	{
		for (TActorIterator<AFlowFieldActor> It(World); It; ++It)
		{
			FlowFieldActor = *It;
			break;
		}
	}

	if (!FlowFieldActor)
	{
		UE_LOG(LogTemp, Error, TEXT("[FlowFieldSubsystem] ScanAndPlaceObstacles: no FlowFieldActor"));
		return;
	}

	FVector Min, Max;
	if (!FlowFieldActor->ResolveBounds(Min, Max))
	{
		UE_LOG(LogTemp, Error, TEXT("[FlowFieldSubsystem] ResolveBounds failed"));
		return;
	}

	float CellSize = FlowFieldActor->CellSize;
	int32 W = FMath::CeilToInt((Max.X - Min.X) / CellSize);
	int32 H = FMath::CeilToInt((Max.Y - Min.Y) / CellSize);
	int32 Total = W * H;

	ScanState = MakeShared<FScanState>();
	ScanState->CellInfosPtr = MakeShared<TArray<FCellScanInfo>>();
	ScanState->CellInfosPtr->SetNum(Total);
	ScanState->DelegatesPtr = MakeShared<TArray<FTraceDelegate>>();
	ScanState->DelegatesPtr->SetNum(Total);
	ScanState->PendingCount = MakeShared<FThreadSafeCounter>(Total);
	ScanState->Min  = Min;
	ScanState->MaxZ = Max.Z;
	ScanState->W = W;
	ScanState->H = H;
	ScanState->CellSize = CellSize;
	ScanState->Half = CellSize * 0.5f;
	ScanState->MaxStepH = FlowFieldActor->MaxStepHeight;
	ScanState->Total = Total;
	ScanState->SubmitIdx = 0;
	ScanState->bSubmitDone = false;

	ScanState->bGroundLevelScan  = (FlowFieldActor->ScanMode == EFlowFieldScanMode::GroundLevel);
	ScanState->WalkableThreshold = FMath::Cos(FMath::DegreesToRadians(
		FMath::Clamp(FlowFieldActor->MaxWalkSlope, 0.f, 89.f)));

	ScanBatchSize = FMath::Clamp(BatchSize, 10, 2000);

	UE_LOG(LogTemp, Log, TEXT("[FlowFieldSubsystem] Scan starting: %d x %d = %d cells, batch=%d"),
	       W, H, Total, ScanBatchSize);
}

// ── SubmitNextBatch ───────────────────────────────────────────────

void UFlowFieldSubsystem::SubmitNextBatch()
{
	if (!ScanState.IsValid()) return;

	UWorld* World = GetWorld();
	if (!World) return;

	FScanState& S = *ScanState;
	int32 BatchEnd = FMath::Min(S.SubmitIdx + ScanBatchSize, S.Total);

	FCollisionQueryParams QueryParams;
	QueryParams.bTraceComplex = false;

	static const float RayStartHeight = 5000.f;
	static const float RayLength = 10000.f;

	TWeakObjectPtr<UFlowFieldSubsystem> WeakSelf(this);
	const bool bGroundLevel         = S.bGroundLevelScan;
	const float WalkableThreshold   = S.WalkableThreshold;
	const float ScanMinZ            = S.Min.Z;
	const float ScanMaxZ            = S.MaxZ;
	const EAsyncTraceType TraceType = bGroundLevel
		? EAsyncTraceType::Multi
		: EAsyncTraceType::Single;

	for (int32 Idx = S.SubmitIdx; Idx < BatchEnd; ++Idx)
	{
		int32 X = Idx % S.W;
		int32 Y = Idx / S.W;

		FVector Start(
			S.Min.X + X * S.CellSize + S.Half,
			S.Min.Y + Y * S.CellSize + S.Half,
			S.Min.Z + RayStartHeight
		);
		FVector End = Start - FVector(0.f, 0.f, RayLength);

		FTraceDelegate& Delegate = (*S.DelegatesPtr)[Idx];
		Delegate.BindLambda([WeakSelf,
				CellInfosPtr = S.CellInfosPtr,
				PendingCount = S.PendingCount,
				DelegatesPtr = S.DelegatesPtr,
				bGroundLevel,
				WalkableThreshold,
				ScanMinZ,
				ScanMaxZ,
				Idx](const FTraceHandle&, FTraceDatum& Data)
			{
				FCellScanInfo& Info = (*CellInfosPtr)[Idx];

				if (bGroundLevel)
				{
					// 穿透模式：
					// 1. 只保留 Z 在 BoundsBox 范围内的命中（排除范围外的屋顶、地下室）
					// 2. 过滤法线朝下的面（内表面、顶棚底面）
					// 3. 取剩余命中中 Z 最低的可行走面
					float LowestZ      = FLT_MAX;
					FVector BestNormal = FVector::UpVector;
					for (const FHitResult& Hit : Data.OutHits)
					{
						if (!Hit.IsValidBlockingHit()) continue;
						if (Hit.Location.Z < ScanMinZ || Hit.Location.Z > ScanMaxZ) continue; // 超出本层范围
						if (Hit.Normal.Z < WalkableThreshold) continue;                        // 非可行走面
						if (Hit.Location.Z < LowestZ)
						{
							LowestZ    = Hit.Location.Z;
							BestNormal = Hit.Normal;
						}
					}
					if (LowestZ != FLT_MAX)
					{
						Info.bHit     = true;
						Info.SurfaceZ = LowestZ;
						Info.Normal   = BestNormal;
					}
				}
				else
				{
					// TopDown 模式：原始行为，取第一个命中
					if (Data.OutHits.Num() > 0 && Data.OutHits[0].IsValidBlockingHit())
					{
						Info.bHit     = true;
						Info.SurfaceZ = Data.OutHits[0].Location.Z;
						Info.Normal   = Data.OutHits[0].Normal;
					}
				}

				if (PendingCount->Decrement() == 0)
				{
					if (UFlowFieldSubsystem* Self = WeakSelf.Get())
						Self->FinalizeScan();
				}
			});

		FCollisionObjectQueryParams ObjQuery;
		// ObjQuery.AddObjectTypesToQuery(ECC_WorldStatic);
		ObjQuery.AddObjectTypesToQuery(ECC_GameTraceChannel1); // Terrain

		World->AsyncLineTraceByObjectType(
			TraceType,
			Start, End,
			ObjQuery,
			QueryParams,
			&Delegate
		);
	}

	S.SubmitIdx = BatchEnd;

	if (OnScanProgressUpdated.IsBound())
		OnScanProgressUpdated.Execute(S.SubmitIdx);

	UE_LOG(LogTemp, Log, TEXT("[FlowFieldSubsystem] Submitted %d / %d"), S.SubmitIdx, S.Total);

	if (S.SubmitIdx >= S.Total)
	{
		S.bSubmitDone = true;
		UE_LOG(LogTemp, Log, TEXT("[FlowFieldSubsystem] All traces submitted, awaiting results..."));
	}
}

// ── FinalizeScan ──────────────────────────────────────────────────

void UFlowFieldSubsystem::FinalizeScan()
{
	if (!ScanState.IsValid() || !FlowFieldActor) return;

	UWorld* World = GetWorld();
	if (!World) return;

	FScanState& S = *ScanState;
	auto& CellInfos = *S.CellInfosPtr;
	int32 W = S.W, H = S.H, Total = S.Total;

	FlowFieldActor->SavedSurfaceZ.SetNum(Total);
	FlowFieldActor->SavedNormals.SetNum(Total);
	FlowFieldActor->SavedGridWidth = W;
	FlowFieldActor->SavedGridHeight = H;
	for (int32 i = 0; i < Total; ++i)
	{
		FlowFieldActor->SavedSurfaceZ[i] = CellInfos[i].bHit ? CellInfos[i].SurfaceZ : 0.f;
		FlowFieldActor->SavedNormals[i] = CellInfos[i].Normal;
	}

	static const FIntPoint Dirs4[] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};

	TArray<FIntPoint> ObstacleCells;
	for (int32 Y = 0; Y < H; ++Y)
	{
		for (int32 X = 0; X < W; ++X)
		{
			const FCellScanInfo& Info = CellInfos[Y * W + X];
			if (!Info.bHit) continue;

			for (const FIntPoint& D : Dirs4)
			{
				int32 NX = X + D.X, NY = Y + D.Y;
				if (NX < 0 || NY < 0 || NX >= W || NY >= H) continue;
				const FCellScanInfo& N = CellInfos[NY * W + NX];
				if (!N.bHit) continue;
				if (FMath::Abs(Info.SurfaceZ - N.SurfaceZ) > S.MaxStepH)
				{
					ObstacleCells.Add({X, Y});
					break;
				}
			}
		}
	}

	// 直接烘焙进网格，不 Spawn Actor
	FlowFieldActor->BakeObstaclesFromScan(W, H, ObstacleCells);

	const int32 Placed = ObstacleCells.Num();
	UE_LOG(LogTemp, Log, TEXT("[FlowFieldSubsystem] FinalizeScan: %d obstacle cells baked"), Placed);

	if (OnScanCompleted.IsBound())
		OnScanCompleted.Execute(Placed);

	OnScanProgressUpdated.Unbind();
	OnScanCompleted.Unbind();
	ScanState.Reset();
}

// ── ClearObstacleActors ───────────────────────────────────────────

void UFlowFieldSubsystem::ClearObstacleActors()
{
	if (ScanState.IsValid())
	{
		OnScanProgressUpdated.Unbind();
		OnScanCompleted.Unbind();
		ScanState.Reset();
	}

	if (FlowFieldActor)
		FlowFieldActor->ClearBakedObstacles();

	UWorld* World = GetWorld();
	if (!World) return;

	FlushPersistentDebugLines(World);
	FlushDebugStrings(World);

	UE_LOG(LogTemp, Log, TEXT("[FlowFieldSubsystem] ClearObstacleActors: baked obstacles cleared"));
}
