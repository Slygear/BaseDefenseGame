// UBuildSystem.cpp tam hali - Tüm compile error'ları düzeltildi
#include "UBuildSystem.h"
#include "Net/UnrealNetwork.h"
#include "Engine/StaticMeshActor.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "Components/StaticMeshComponent.h"
#include "Kismet/KismetSystemLibrary.h"
#include "GameFramework/Character.h"
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"

UBuildSystem::UBuildSystem()
{
    PrimaryComponentTick.bCanEverTick = true;
    SetIsReplicatedByDefault(true);

    bBuildModeActive = false;
    CurrentBlockType = EBlockType::Grass;
    bHasValidPlacement = false;
    bIsCurrentBlockFunctional = false;
    bCanCurrentBlockRotate = false;
}

void UBuildSystem::BeginPlay()
{
    Super::BeginPlay();

    InitializeDebugSystem();

    AActor* Owner = GetOwner();
    if (Owner)
    {
        GhostBlockMesh = NewObject<UStaticMeshComponent>(Owner, TEXT("GhostBlockMesh"));
        GhostBlockMesh->SetAbsolute(true, true, true);
        GhostBlockMesh->RegisterComponent();
        GhostBlockMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        GhostBlockMesh->SetVisibility(false);
        GhostBlockMesh->SetCastShadow(false);
    }
}

void UBuildSystem::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

    APawn* Pawn = Cast<APawn>(GetOwner());
    if (Pawn && Pawn->IsLocallyControlled() && bBuildModeActive)
    {
        UpdateGhostBlock();
    }
}

void UBuildSystem::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);

    DOREPLIFETIME(UBuildSystem, CurrentBlockType);
    DOREPLIFETIME(UBuildSystem, bBuildModeActive);
    DOREPLIFETIME(UBuildSystem, CurrentBuildRowIndex);
    DOREPLIFETIME(UBuildSystem, CurrentBlockSize);
}

void UBuildSystem::ActivateBuildMode(EBlockType BlockType)
{
    CurrentBlockType = BlockType;
    bBuildModeActive = true;

    if (GetOwnerRole() == ROLE_Authority)
    {
        // Server already activated build mode
    }
    else
    {
        ServerActivateBuildMode(BlockType);
    }

    if (BlockDataTable && GhostBlockMesh)
    {
        FString BlockTypeStr = UEnum::GetValueAsString(BlockType).Replace(TEXT("EBlockType::"), TEXT(""));
        FBlockData* BlockData = BlockDataTable->FindRow<FBlockData>(FName(*BlockTypeStr), TEXT(""));

        if (BlockData && BlockData->BlockMesh)
        {
            GhostBlockMesh->SetStaticMesh(BlockData->BlockMesh);
            GhostBlockMesh->SetVisibility(true);

            CurrentItemName = BlockData->ItemName;
            bIsCurrentBlockFunctional = BlockData->bIsFunctionalBlock;
            bCanCurrentBlockRotate = BlockData->bUseBlockRotation;

            if (BlockData->BlockMaterial)
            {
                GhostBlockMesh->SetMaterial(0, BlockData->BlockMaterial);
            }

            if (MapGenerator.IsValid())
            {
                float ScaleFactor = 1.0f;

                if (bIsCurrentBlockFunctional && BlockData->ActorClass)
                {
                    AActor* DefaultActor = BlockData->ActorClass->GetDefaultObject<AActor>();
                    if (DefaultActor)
                    {
                        UStaticMeshComponent* ActorMeshComp = DefaultActor->FindComponentByClass<UStaticMeshComponent>();
                        if (ActorMeshComp && ActorMeshComp->GetStaticMesh())
                        {
                            FBoxSphereBounds ActorBounds = ActorMeshComp->GetStaticMesh()->GetBounds();
                            float ActorMeshSize = ActorBounds.BoxExtent.GetMax() * 2.0f;

                            FBoxSphereBounds GhostBounds = BlockData->BlockMesh->GetBounds();
                            float GhostMeshSize = GhostBounds.BoxExtent.GetMax() * 2.0f;

                            if (ActorMeshSize > 1.0f && GhostMeshSize > 1.0f)
                            {
                                ScaleFactor = (ActorMeshSize / GhostMeshSize);
                            }
                            else
                            {
                                ScaleFactor = 1.0f * BlockData->BlockSize;
                            }
                        }
                    }
                }
                else
                {
                    float MeshSize = 100.0f;
                    FBoxSphereBounds Bounds = BlockData->BlockMesh->GetBounds();
                    float MaxExtent = Bounds.BoxExtent.GetMax() * 2.0f;

                    if (MaxExtent > 1.0f)
                    {
                        MeshSize = MaxExtent;
                    }

                    float TargetSize = MapGenerator->BlockSize * BlockData->BlockSize;
                    ScaleFactor = TargetSize / MeshSize;
                }

                GhostBlockMesh->SetWorldScale3D(FVector(ScaleFactor));
            }
            else
            {
                GhostBlockMesh->SetWorldScale3D(FVector(0.8f * BlockData->BlockSize));
            }

            if (!bCanCurrentBlockRotate)
            {
                GhostBlockMesh->SetWorldRotation(FRotator::ZeroRotator);
            }
            else
            {
                GhostBlockMesh->SetWorldRotation(FRotator(0, 0, 0));
            }
        }
    }
}

bool UBuildSystem::ServerActivateBuildMode_Validate(EBlockType BlockType)
{
    return true;
}

void UBuildSystem::ServerActivateBuildMode_Implementation(EBlockType BlockType)
{
    CurrentBlockType = BlockType;
    bBuildModeActive = true;
}

void UBuildSystem::ActivateBuildModeByRowIndex(int32 RowIndex)
{
    if (!BlockDataTable)
    {
        UE_LOG(LogTemp, Warning, TEXT("BlockDataTable is null"));
        return;
    }

    TArray<FName> RowNames = BlockDataTable->GetRowNames();

    if (!RowNames.IsValidIndex(RowIndex))
    {
        UE_LOG(LogTemp, Warning, TEXT("Invalid row index: %d. DataTable has %d rows."), RowIndex, RowNames.Num());
        return;
    }

    FName RowName = RowNames[RowIndex];
    FBlockData* BlockData = BlockDataTable->FindRow<FBlockData>(RowName, TEXT(""));

    if (!BlockData)
    {
        UE_LOG(LogTemp, Warning, TEXT("Could not find row with index %d (name: %s)"), RowIndex, *RowName.ToString());
        return;
    }

    CurrentBuildRowIndex = RowIndex;
    CurrentBlockType = BlockData->BlockType;
    CurrentBlockSize = BlockData->BlockSize;
    CurrentItemName = BlockData->ItemName;

    bIsCurrentBlockFunctional = BlockData->bIsFunctionalBlock;
    bCanCurrentBlockRotate = BlockData->bUseBlockRotation;

    bBuildModeActive = true;

    if (GetOwnerRole() != ROLE_Authority)
    {
        ServerActivateBuildModeByRowIndex(RowIndex);
    }

    ChangeMesh(RowIndex);
}

bool UBuildSystem::ServerActivateBuildModeByRowIndex_Validate(int32 RowIndex)
{
    return true;
}

void UBuildSystem::ServerActivateBuildModeByRowIndex_Implementation(int32 RowIndex)
{
    if (!BlockDataTable)
        return;

    TArray<FName> RowNames = BlockDataTable->GetRowNames();

    if (!RowNames.IsValidIndex(RowIndex))
        return;

    FName RowName = RowNames[RowIndex];
    FBlockData* BlockData = BlockDataTable->FindRow<FBlockData>(RowName, TEXT(""));

    if (!BlockData)
        return;

    CurrentBuildRowIndex = RowIndex;
    CurrentBlockType = BlockData->BlockType;
    CurrentBlockSize = BlockData->BlockSize;

    bBuildModeActive = true;
}

void UBuildSystem::ChangeMesh(int32 RowIndex)
{
    if (!GhostBlockMesh || !BlockDataTable)
        return;

    TArray<FName> RowNames = BlockDataTable->GetRowNames();

    if (!RowNames.IsValidIndex(RowIndex))
    {
        UE_LOG(LogTemp, Warning, TEXT("ChangeMesh: Invalid row index: %d. DataTable has %d rows."), RowIndex, RowNames.Num());
        return;
    }

    FName RowName = RowNames[RowIndex];
    FBlockData* BlockData = BlockDataTable->FindRow<FBlockData>(RowName, TEXT(""));

    if (!BlockData || !BlockData->BlockMesh)
    {
        UE_LOG(LogTemp, Warning, TEXT("ChangeMesh: Could not find valid block data for row index %d"), RowIndex);
        return;
    }

    CurrentBuildRowIndex = RowIndex;
    CurrentBlockType = BlockData->BlockType;
    CurrentBlockSize = BlockData->BlockSize;
    CurrentItemName = BlockData->ItemName;

    bIsCurrentBlockFunctional = BlockData->bIsFunctionalBlock;
    bCanCurrentBlockRotate = BlockData->bUseBlockRotation;

    GhostBlockMesh->SetStaticMesh(BlockData->BlockMesh);

    if (BlockData->BlockMaterial)
    {
        GhostBlockMesh->SetMaterial(0, BlockData->BlockMaterial);
    }

    if (MapGenerator.IsValid())
    {
        float ScaleFactor = 1.0f;

        if (BlockData->bIsFunctionalBlock && BlockData->ActorClass)
        {
            AActor* DefaultActor = BlockData->ActorClass->GetDefaultObject<AActor>();
            if (DefaultActor)
            {
                UStaticMeshComponent* ActorMeshComp = DefaultActor->FindComponentByClass<UStaticMeshComponent>();
                if (ActorMeshComp && ActorMeshComp->GetStaticMesh() && BlockData->BlockMesh)
                {
                    FBoxSphereBounds ActorBounds = ActorMeshComp->GetStaticMesh()->GetBounds();
                    FBoxSphereBounds GhostBounds = BlockData->BlockMesh->GetBounds();

                    float ActorDiameter = ActorBounds.BoxExtent.GetMax() * 2.0f;
                    float GhostDiameter = GhostBounds.BoxExtent.GetMax() * 2.0f;

                    float AdjustmentFactor = 1.0f;

                    if (ActorDiameter > 0.0f && GhostDiameter > 0.0f)
                    {
                        ScaleFactor = (ActorDiameter / GhostDiameter) * AdjustmentFactor;
                    }
                }
            }
        }
        else
        {
            float MeshSize = 100.0f;

            FBoxSphereBounds Bounds = BlockData->BlockMesh->GetBounds();
            float MaxExtent = Bounds.BoxExtent.GetMax() * 2.0f;

            if (MaxExtent > 1.0f)
            {
                MeshSize = MaxExtent;
            }

            float TargetSize = MapGenerator->BlockSize;
            ScaleFactor = TargetSize / MeshSize;
        }

        GhostBlockMesh->SetWorldScale3D(FVector(ScaleFactor));
    }
    else
    {
        GhostBlockMesh->SetWorldScale3D(FVector(0.8f));
    }

    if (!bCanCurrentBlockRotate)
    {
        GhostBlockMesh->SetWorldRotation(FRotator::ZeroRotator);
    }

    if (bBuildModeActive)
    {
        GhostBlockMesh->SetVisibility(true);
    }
}

void UBuildSystem::RotateGhostBlock(float RotationDelta)
{
    if (!GhostBlockMesh || !bBuildModeActive || !bCanCurrentBlockRotate)
        return;

    FRotator CurrentRotation = GhostBlockMesh->GetComponentRotation();
    FRotator NewRotation = FRotator(0.0f, CurrentRotation.Yaw + RotationDelta, 0.0f);

    float SnapAngle = 45.0f;
    NewRotation.Yaw = FMath::GridSnap(NewRotation.Yaw, SnapAngle);

    GhostBlockMesh->SetWorldRotation(NewRotation);
}

bool UBuildSystem::IsFunctionalBlockAt(const FVector& Location)
{
    if (!GetWorld() || !MapGenerator.IsValid())
        return false;

    float EffectiveBlockSize = MapGenerator->BlockSize + MapGenerator->BlockSpacing;
    float CheckRadius = EffectiveBlockSize * 0.45f;

    bool bIsServer = (GetOwnerRole() == ROLE_Authority);

    UE_LOG(LogTemp, Warning, TEXT("[%s] === IsFunctionalBlockAt DEBUG ==="),
        bIsServer ? TEXT("SERVER") : TEXT("CLIENT"));
    UE_LOG(LogTemp, Warning, TEXT("[%s] Location: %s, CheckRadius: %f, BlockSize: %f"),
        bIsServer ? TEXT("SERVER") : TEXT("CLIENT"), *Location.ToString(), CheckRadius, EffectiveBlockSize);

    // Base Core kontrolü
    if (MapGenerator->SpawnedBaseCore)
    {
        FVector BaseCoreLoc = MapGenerator->SpawnedBaseCore->GetActorLocation();
        float DistanceToBaseCore = FVector::Dist(Location, BaseCoreLoc);

        if (DistanceToBaseCore < EffectiveBlockSize * 0.7f)
        {
            UE_LOG(LogTemp, Warning, TEXT("[%s] *** BASE CORE ÇAKIŞMASI *** - Mesafe: %f < %f"),
                bIsServer ? TEXT("SERVER") : TEXT("CLIENT"), DistanceToBaseCore, EffectiveBlockSize * 0.7f);
            return true;
        }
    }

    int32 FunctionalBlocksFound = 0;

    // Sphere overlap ile functional block kontrolü
    TArray<AActor*> OverlappingActors;
    UKismetSystemLibrary::SphereOverlapActors(
        GetWorld(),
        Location,
        CheckRadius,
        TArray<TEnumAsByte<EObjectTypeQuery>>(),
        AActor::StaticClass(),
        TArray<AActor*>{ GetOwner() },
        OverlappingActors
    );

    UE_LOG(LogTemp, Warning, TEXT("[%s] SphereOverlap buldu %d actor (radius: %f)"),
        bIsServer ? TEXT("SERVER") : TEXT("CLIENT"), OverlappingActors.Num(), CheckRadius);

    for (AActor* Actor : OverlappingActors)
    {
        if (!Actor || Actor == GetOwner() || Actor == MapGenerator->SpawnedBaseCore)
            continue;

        UStaticMeshComponent* MeshComp = Actor->FindComponentByClass<UStaticMeshComponent>();
        if (MeshComp)
        {
            bool bHasTag = MeshComp->ComponentHasTag(TEXT("FunctionalBlock"));
            float Distance = FVector::Dist(Location, Actor->GetActorLocation());

            UE_LOG(LogTemp, Warning, TEXT("[%s] Actor: %s, HasTag: %s, Distance: %f"),
                bIsServer ? TEXT("SERVER") : TEXT("CLIENT"),
                *Actor->GetName(),
                bHasTag ? TEXT("YES") : TEXT("NO"),
                Distance);

            if (bHasTag)
            {
                FunctionalBlocksFound++;
                UE_LOG(LogTemp, Warning, TEXT("[%s] *** FUNCTIONAL BLOCK BULUNDU *** %s (Distance: %f)"),
                    bIsServer ? TEXT("SERVER") : TEXT("CLIENT"), *Actor->GetName(), Distance);
                return true;
            }
        }
    }

    // Manual arama - overlap başarısızsa
    UE_LOG(LogTemp, Warning, TEXT("[%s] Manuel arama başlatılıyor..."),
        bIsServer ? TEXT("SERVER") : TEXT("CLIENT"));

    for (TActorIterator<AActor> ActorItr(GetWorld()); ActorItr; ++ActorItr)
    {
        AActor* Actor = *ActorItr;
        if (!Actor || Actor == GetOwner() || Actor == MapGenerator->SpawnedBaseCore)
            continue;

        float Distance = FVector::Dist(Location, Actor->GetActorLocation());
        if (Distance > CheckRadius)
            continue;

        UStaticMeshComponent* MeshComp = Actor->FindComponentByClass<UStaticMeshComponent>();
        if (MeshComp)
        {
            bool bHasTag = MeshComp->ComponentHasTag(TEXT("FunctionalBlock"));

            if (bHasTag)
            {
                FunctionalBlocksFound++;
                UE_LOG(LogTemp, Warning, TEXT("[%s] *** MANUEL: FUNCTIONAL BLOCK BULUNDU *** %s (Distance: %f)"),
                    bIsServer ? TEXT("SERVER") : TEXT("CLIENT"), *Actor->GetName(), Distance);
                return true;
            }
        }
    }

    UE_LOG(LogTemp, Warning, TEXT("[%s] === SONUÇ: Functional block çakışması YOK === (Toplam bulunan: %d)"),
        bIsServer ? TEXT("SERVER") : TEXT("CLIENT"), FunctionalBlocksFound);
    return false;
}

bool UBuildSystem::ApplyDamageToBlock(const FVector& Location, float Damage, AActor* EventInstigator, AActor* DamageCauser, TSubclassOf<UDamageType> DamageTypeClass)
{
    if (!MapGenerator.IsValid())
        return false;

    FHitResult HitResult;

    FVector ViewLocation;
    FRotator ViewRotation;
    GetPlayerViewPoint(ViewLocation, ViewRotation);

    FVector TraceStart = ViewLocation + ViewRotation.Vector() * 50.0f;
    FVector TraceEnd = ViewLocation + ViewRotation.Vector() * BuildDistance;

    FCollisionQueryParams QueryParams;
    QueryParams.AddIgnoredActor(GetOwner());

    DrawDebugLineIfEnabled(EDebugCategory::BuildSystem, TraceStart, TraceEnd, FColor::Red);

    if (GetWorld()->LineTraceSingleByChannel(HitResult, TraceStart, TraceEnd, ECC_Visibility, QueryParams))
    {
        FVector HitLocation = HitResult.Location;
        FVector AdjustedLocation = HitLocation - HitResult.Normal * 5.0f;

        DrawDebugSphereIfEnabled(EDebugCategory::BlockPlacement, HitLocation, 10.0f, FColor::Yellow);
        DrawDebugSphereIfEnabled(EDebugCategory::BlockPlacement, AdjustedLocation, 8.0f, FColor::Green);

        EBlockType BlockType = MapGenerator->GetBlockTypeAtPosition(AdjustedLocation);

        if (BlockType == EBlockType::Air)
        {
            return false;
        }

        if (GetOwnerRole() != ROLE_Authority)
        {
            ServerApplyDamageToBlock(AdjustedLocation, Damage, EventInstigator, DamageCauser, DamageTypeClass);
            return true;
        }

        MapGenerator->ApplyDamageToBlock(AdjustedLocation, Damage, EventInstigator, DamageCauser, DamageTypeClass);
        return true;
    }

    return false;
}

bool UBuildSystem::ServerApplyDamageToBlock_Validate(const FVector& Location, float Damage, AActor* EventInstigator, AActor* DamageCauser, TSubclassOf<UDamageType> DamageTypeClass)
{
    return true;
}

void UBuildSystem::ServerApplyDamageToBlock_Implementation(const FVector& Location, float Damage, AActor* EventInstigator, AActor* DamageCauser, TSubclassOf<UDamageType> DamageTypeClass)
{
    if (!MapGenerator.IsValid())
        return;

    EBlockType BlockType = MapGenerator->GetBlockTypeAtPosition(Location);

    if (BlockType == EBlockType::Air)
        return;

    MapGenerator->ApplyDamageToBlock(Location, Damage, EventInstigator, DamageCauser, DamageTypeClass);
}

void UBuildSystem::DeactivateBuildMode()
{
    bBuildModeActive = false;

    if (GetOwnerRole() == ROLE_Authority)
    {
        // Server already deactivated build mode
    }
    else
    {
        ServerDeactivateBuildMode();
    }

    if (GhostBlockMesh)
    {
        GhostBlockMesh->SetVisibility(false);
    }
}

bool UBuildSystem::ServerDeactivateBuildMode_Validate()
{
    return true;
}

void UBuildSystem::ServerDeactivateBuildMode_Implementation()
{
    bBuildModeActive = false;
}

bool UBuildSystem::TryPlaceBlock()
{
    if (!bBuildModeActive || !bHasValidPlacement)
        return false;

    if (!BlockDataTable)
        return false;

    TArray<FName> RowNames = BlockDataTable->GetRowNames();

    if (!RowNames.IsValidIndex(CurrentBuildRowIndex))
        return false;

    FName RowName = RowNames[CurrentBuildRowIndex];
    FBlockData* BlockData = BlockDataTable->FindRow<FBlockData>(RowName, TEXT(""));

    if (!BlockData)
        return false;

    if (BlockData->bIsFunctionalBlock)
    {
        if (!BlockData->ActorClass)
            return false;

        FRotator SpawnRotation = FRotator::ZeroRotator;
        if (BlockData->bUseBlockRotation && GhostBlockMesh)
        {
            SpawnRotation = GhostBlockMesh->GetComponentRotation();
            SpawnRotation.Pitch = 0.0f;
            SpawnRotation.Roll = 0.0f;
        }

        ServerPlaceFunctionalBlockFromTable(BlockData->ActorClass, GhostBlockLocation, SpawnRotation, CurrentBlockType, CurrentItemName);
        return true;
    }
    else
    {
        if (MapGenerator.IsValid())
        {
            ServerPlaceBlock(GhostBlockLocation, CurrentBlockType);
            return true;
        }
    }

    return false;
}

bool UBuildSystem::ServerPlaceBlock_Validate(const FVector& Location, EBlockType BlockType)
{
    return true;
}

void UBuildSystem::ServerPlaceBlock_Implementation(const FVector& Location, EBlockType BlockType)
{
    if (!MapGenerator.IsValid() || !CanPlaceBlockAt(Location, BlockType))
        return;

    FName ItemName = MapGenerator->GetItemNameForBlockType(BlockType);

    MapGenerator->SetBlockTypeAtPosition(Location, BlockType);

    OnBlockPlaced.Broadcast(Location, BlockType, ItemName);
}

bool UBuildSystem::TryRemoveBlock()
{
    if (!bBuildModeActive || !MapGenerator.IsValid())
        return false;

    FVector ViewLocation;
    FRotator ViewRotation;
    GetPlayerViewPoint(ViewLocation, ViewRotation);

    FVector TraceStart = ViewLocation + ViewRotation.Vector() * 50.0f;
    FVector TraceEnd = ViewLocation + ViewRotation.Vector() * BuildDistance;
    FHitResult HitResult;

    FCollisionQueryParams QueryParams;
    QueryParams.AddIgnoredActor(GetOwner());

    DrawDebugLineIfEnabled(EDebugCategory::BuildSystem, TraceStart, TraceEnd, FColor::Red);

    if (GetWorld()->LineTraceSingleByChannel(HitResult, TraceStart, TraceEnd, ECC_Visibility, QueryParams))
    {
        DrawDebugSphereIfEnabled(EDebugCategory::BlockPlacement, HitResult.Location, 10.0f, FColor::Green);

        FVector AdjustedHitLocation = HitResult.Location - HitResult.Normal * 5.0f;

        EBlockType HitBlockType = MapGenerator->GetBlockTypeAtPosition(AdjustedHitLocation);

        if (HitBlockType == EBlockType::Air)
        {
            return false;
        }

        ServerRemoveBlock(AdjustedHitLocation);
        return true;
    }

    return false;
}

bool UBuildSystem::ServerRemoveBlock_Validate(const FVector& Location)
{
    return true;
}

void UBuildSystem::ServerRemoveBlock_Implementation(const FVector& Location)
{
    if (!MapGenerator.IsValid())
        return;

    EBlockType BlockType = MapGenerator->GetBlockTypeAtPosition(Location);

    if (BlockType == EBlockType::Air)
        return;

    float DamageAmount = 50.0f;
    MapGenerator->ApplyDamageToBlock(Location, DamageAmount, GetOwner(), nullptr, nullptr);
}

bool UBuildSystem::CanPlaceBlockAt(const FVector& Location, EBlockType BlockType)
{
    if (!MapGenerator.IsValid())
        return false;

    bool bIsServer = (GetOwnerRole() == ROLE_Authority);

    // Functional block kontrolü
    bool bBlockedByFunctional = IsFunctionalBlockAt(Location);
    if (bBlockedByFunctional)
    {
        UE_LOG(LogTemp, Warning, TEXT("[%s] CanPlaceBlockAt: Functional block engeli"),
            bIsServer ? TEXT("SERVER") : TEXT("CLIENT"));
        return false;
    }

    EBlockType ExistingBlockType = MapGenerator->GetBlockTypeAtPosition(Location);

    // *** YENİ KONTROL: INVISIBLE WALL ÜZERİNE BİR ŞEY YERLEŞTİRİLEMEZ ***
    if (ExistingBlockType == EBlockType::InvisibleWall)
    {
        UE_LOG(LogTemp, Warning, TEXT("[%s] CanPlaceBlockAt: Bu konumda Invisible Wall var, blok yerleştirilemez"),
            bIsServer ? TEXT("SERVER") : TEXT("CLIENT"));
        return false;
    }

    // Normal Air kontrolü
    if (ExistingBlockType != EBlockType::Air)
    {
        return false;
    }

    float EffectiveBlockSize = MapGenerator->BlockSize + MapGenerator->BlockSpacing;

    int32 BlockSize = 1;
    int32 RequiredSupportBlocks = 1;
    bool bIsFunctionalBlock = false;

    if (BlockDataTable)
    {
        TArray<FName> RowNames = BlockDataTable->GetRowNames();
        if (RowNames.IsValidIndex(CurrentBuildRowIndex))
        {
            FName RowName = RowNames[CurrentBuildRowIndex];
            FBlockData* BlockData = BlockDataTable->FindRow<FBlockData>(RowName, TEXT("Searching block data"));

            if (BlockData)
            {
                BlockSize = BlockData->BlockSize;
                bIsFunctionalBlock = BlockData->bIsFunctionalBlock;
                RequiredSupportBlocks = BlockData->RequiredSupportBlocks;
            }
        }
    }

    if (BlockSize > 1)
    {
        for (int32 XOffset = 0; XOffset < BlockSize; XOffset++)
        {
            for (int32 YOffset = 0; YOffset < BlockSize; YOffset++)
            {
                FVector CheckLocation = Location + FVector(XOffset * EffectiveBlockSize, YOffset * EffectiveBlockSize, 0.0f);

                if (IsFunctionalBlockAt(CheckLocation))
                {
                    return false;
                }

                EBlockType CheckBlockType = MapGenerator->GetBlockTypeAtPosition(CheckLocation);

                // *** YENİ KONTROL: MultiBlock yerleştirme için de InvisibleWall kontrolü ***
                if (CheckBlockType == EBlockType::InvisibleWall)
                {
                    UE_LOG(LogTemp, Warning, TEXT("[%s] CanPlaceBlockAt: MultiBlock alanında Invisible Wall var"),
                        bIsServer ? TEXT("SERVER") : TEXT("CLIENT"));
                    return false;
                }

                if (CheckBlockType != EBlockType::Air)
                {
                    return false;
                }
            }
        }
    }

    if (bIsFunctionalBlock && RequiredSupportBlocks > 0)
    {
        int32 SupportBlocksFound = 0;

        FVector Corner1 = Location + FVector(-EffectiveBlockSize / 2, -EffectiveBlockSize / 2, -EffectiveBlockSize);
        FVector Corner2 = Location + FVector(EffectiveBlockSize / 2, -EffectiveBlockSize / 2, -EffectiveBlockSize);
        FVector Corner3 = Location + FVector(-EffectiveBlockSize / 2, EffectiveBlockSize / 2, -EffectiveBlockSize);
        FVector Corner4 = Location + FVector(EffectiveBlockSize / 2, EffectiveBlockSize / 2, -EffectiveBlockSize);

        TArray<FVector> Corners = { Corner1, Corner2, Corner3, Corner4 };

        for (const FVector& Corner : Corners)
        {
            EBlockType CornerBlockType = MapGenerator->GetBlockTypeAtPosition(Corner);

            // *** GÜNCELLEME: Invisible Wall desteği sayılsın ama aslında ideal değil ***
            // Invisible Wall üzerinde durabilir ama normal bloklar tercih edilir
            if (CornerBlockType != EBlockType::Air)
            {
                SupportBlocksFound++;
            }
        }

        if (SupportBlocksFound < RequiredSupportBlocks)
        {
            return false;
        }
    }
    else if (!bIsFunctionalBlock)
    {
        bool bHasSupport = false;

        FVector BelowLocation = Location - FVector(0.0f, 0.0f, EffectiveBlockSize);
        EBlockType BelowBlockType = MapGenerator->GetBlockTypeAtPosition(BelowLocation);

        // *** GÜNCELLEME: Invisible Wall de destek sayılır ***
        if (BelowBlockType != EBlockType::Air)
        {
            bHasSupport = true;
        }

        if (!bHasSupport)
        {
            FVector Directions[] = {
                FVector(EffectiveBlockSize, 0.0f, 0.0f),
                FVector(-EffectiveBlockSize, 0.0f, 0.0f),
                FVector(0.0f, EffectiveBlockSize, 0.0f),
                FVector(0.0f, -EffectiveBlockSize, 0.0f)
            };

            for (int32 i = 0; i < 4; i++)
            {
                FVector CheckLocation = Location + Directions[i];
                EBlockType SideBlockType = MapGenerator->GetBlockTypeAtPosition(CheckLocation);

                // *** GÜNCELLEME: Invisible Wall de yan destek sayılır ***
                if (SideBlockType != EBlockType::Air)
                {
                    bHasSupport = true;
                    break;
                }
            }
        }

        if (!bHasSupport)
        {
            return false;
        }
    }

    return true;
}


AActor* UBuildSystem::PlaceFunctionalBlock(TSubclassOf<AActor> ActorClass, const FVector& Location, const FRotator& Rotation)
{
    if (!ActorClass || !bHasValidPlacement)
        return nullptr;

    ServerPlaceFunctionalBlock(ActorClass, Location, Rotation);
    return nullptr;
}

bool UBuildSystem::ServerPlaceFunctionalBlock_Validate(TSubclassOf<AActor> ActorClass, const FVector& Location, const FRotator& Rotation)
{
    return true;
}

void UBuildSystem::ServerPlaceFunctionalBlock_Implementation(TSubclassOf<AActor> ActorClass, const FVector& Location, const FRotator& Rotation)
{
    if (!ActorClass || !GetWorld())
        return;

    if (!CanPlaceBlockAt(Location, CurrentBlockType))
        return;

    FActorSpawnParameters SpawnParams;
    SpawnParams.Owner = GetOwner();
    SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

    AActor* NewActor = GetWorld()->SpawnActor<AActor>(ActorClass, Location, Rotation, SpawnParams);
}

bool UBuildSystem::ServerPlaceFunctionalBlockFromTable_Validate(TSubclassOf<AActor> ActorClass, const FVector& Location, const FRotator& Rotation, EBlockType BlockType, FName ItemName)
{
    return true;
}

void UBuildSystem::ServerPlaceFunctionalBlockFromTable_Implementation(TSubclassOf<AActor> ActorClass, const FVector& Location, const FRotator& Rotation, EBlockType BlockType, FName ItemName)
{
    if (!ActorClass || !GetWorld() || !MapGenerator.IsValid())
        return;

    UE_LOG(LogTemp, Warning, TEXT("SERVER: === FUNCTIONAL BLOCK YERLEŞTIRME ==="));
    UE_LOG(LogTemp, Warning, TEXT("SERVER: Location: %s"), *Location.ToString());

    if (!CanPlaceBlockAt(Location, BlockType))
    {
        UE_LOG(LogTemp, Error, TEXT("SERVER: *** YERLEŞTIRME ENGELLENDİ *** - CanPlaceBlockAt false"));
        return;
    }

    UE_LOG(LogTemp, Warning, TEXT("SERVER: CanPlaceBlockAt GEÇTİ - Actor spawn ediliyor..."));

    FActorSpawnParameters SpawnParams;
    SpawnParams.Owner = GetOwner();
    SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;

    AActor* NewActor = GetWorld()->SpawnActor<AActor>(ActorClass, Location, Rotation, SpawnParams);

    if (NewActor)
    {
        UE_LOG(LogTemp, Warning, TEXT("SERVER: Actor başarıyla spawn edildi: %s"), *NewActor->GetName());

        UStaticMeshComponent* MeshComp = NewActor->FindComponentByClass<UStaticMeshComponent>();
        if (MeshComp)
        {
            MeshComp->ComponentTags.AddUnique(FName(TEXT("FunctionalBlock")));
            bool bTagAdded = MeshComp->ComponentHasTag(TEXT("FunctionalBlock"));

            UE_LOG(LogTemp, Warning, TEXT("SERVER: Tag ekleme sonucu: %s"),
                bTagAdded ? TEXT("BAŞARILI") : TEXT("BAŞARISIZ"));

            if (!bTagAdded)
            {
                UE_LOG(LogTemp, Error, TEXT("SERVER: Tag ekleme başarısız, tekrar deneniyor..."));
                MeshComp->ComponentTags.Empty();
                MeshComp->ComponentTags.Add(FName(TEXT("FunctionalBlock")));

                bool bSecondTry = MeshComp->ComponentHasTag(TEXT("FunctionalBlock"));
                UE_LOG(LogTemp, Warning, TEXT("SERVER: İkinci deneme sonucu: %s"),
                    bSecondTry ? TEXT("BAŞARILI") : TEXT("BAŞARISIZ"));
            }
        }
        else
        {
            UE_LOG(LogTemp, Error, TEXT("SERVER: *** HATA *** Static Mesh Component bulunamadı!"));
        }

        FTimerHandle TimerHandle;
        GetWorld()->GetTimerManager().SetTimer(TimerHandle, [this, NewActor]()
            {
                if (NewActor && IsValid(NewActor))
                {
                    MulticastSetFunctionalBlockTag(NewActor);
                }
            }, 0.1f, false);

        OnBlockPlaced.Broadcast(Location, BlockType, ItemName);
        UE_LOG(LogTemp, Warning, TEXT("SERVER: Functional Block yerleştirme tamamlandı!"));
    }
    else
    {
        UE_LOG(LogTemp, Error, TEXT("SERVER: *** HATA *** Actor spawn edilemedi!"));
    }
}

void UBuildSystem::MulticastSetFunctionalBlockTag_Implementation(AActor* Actor)
{
    if (!Actor)
    {
        UE_LOG(LogTemp, Error, TEXT("MulticastSetFunctionalBlockTag: Actor null!"));
        return;
    }

    bool bIsServer = (GetOwnerRole() == ROLE_Authority);

    UE_LOG(LogTemp, Warning, TEXT("[%s] MulticastSetFunctionalBlockTag çağrıldı: %s"),
        bIsServer ? TEXT("SERVER") : TEXT("CLIENT"), *Actor->GetName());

    UStaticMeshComponent* MeshComp = Actor->FindComponentByClass<UStaticMeshComponent>();
    if (MeshComp)
    {
        MeshComp->ComponentTags.AddUnique(FName(TEXT("FunctionalBlock")));

        bool bHasTag = MeshComp->ComponentHasTag(TEXT("FunctionalBlock"));

        UE_LOG(LogTemp, Warning, TEXT("[%s] Tag eklendi: %s - Kontrol: %s"),
            bIsServer ? TEXT("SERVER") : TEXT("CLIENT"),
            *Actor->GetName(),
            bHasTag ? TEXT("BAŞARILI") : TEXT("BAŞARISIZ"));

        if (!bIsServer && !bHasTag)
        {
            UE_LOG(LogTemp, Error, TEXT("CLIENT: Tag ekleme başarısız, tekrar deneniyor..."));

            MeshComp->ComponentTags.Remove(FName(TEXT("FunctionalBlock")));
            MeshComp->ComponentTags.Add(FName(TEXT("FunctionalBlock")));

            bool bFinalCheck = MeshComp->ComponentHasTag(TEXT("FunctionalBlock"));
            UE_LOG(LogTemp, Warning, TEXT("CLIENT: İkinci deneme sonucu: %s"),
                bFinalCheck ? TEXT("BAŞARILI") : TEXT("BAŞARISIZ"));
        }
    }
    else
    {
        UE_LOG(LogTemp, Error, TEXT("[%s] Static Mesh Component bulunamadı: %s"),
            bIsServer ? TEXT("SERVER") : TEXT("CLIENT"), *Actor->GetName());
    }
}

void UBuildSystem::GetPlayerViewPoint(FVector& Location, FRotator& Rotation) const
{
    APawn* OwnerPawn = Cast<APawn>(GetOwner());
    if (OwnerPawn)
    {
        APlayerController* PC = Cast<APlayerController>(OwnerPawn->GetController());
        if (PC)
        {
            PC->GetPlayerViewPoint(Location, Rotation);
            return;
        }
    }

    AActor* Owner = GetOwner();
    if (Owner)
    {
        Location = Owner->GetActorLocation();
        Rotation = Owner->GetActorRotation();
    }
}

void UBuildSystem::UpdateGhostBlock()
{
    if (!GhostBlockMesh || !MapGenerator.IsValid())
        return;

    bool bIsServer = (GetOwnerRole() == ROLE_Authority);

    FVector ViewLocation;
    FRotator ViewRotation;
    GetPlayerViewPoint(ViewLocation, ViewRotation);

    FVector TraceStart = ViewLocation;
    FVector TraceEnd = ViewLocation + ViewRotation.Vector() * BuildDistance;

    FHitResult HitResult;
    FCollisionQueryParams QueryParams;
    QueryParams.AddIgnoredActor(GetOwner());

    bool bHitSomething = GetWorld()->LineTraceSingleByChannel(HitResult, TraceStart, TraceEnd, ECC_Visibility, QueryParams);

    if (bHitSomething)
    {
        // Karakter kontrolü
        if (HitResult.GetActor())
        {
            APawn* HitPawn = Cast<APawn>(HitResult.GetActor());
            ACharacter* HitCharacter = Cast<ACharacter>(HitResult.GetActor());

            if (HitPawn || HitCharacter)
            {
                bHasValidPlacement = false;
                GhostBlockMesh->SetVisibility(false);
                return;
            }
        }

        float EffectiveBlockSize = MapGenerator->BlockSize + MapGenerator->BlockSpacing;

        if (bIsCurrentBlockFunctional)
        {
            UE_LOG(LogTemp, Warning, TEXT("[%s] ===== FUNCTIONAL BLOCK GHOST GÜNCELLE ====="),
                bIsServer ? TEXT("SERVER") : TEXT("CLIENT"));

            float WorldX = HitResult.Location.X;
            float WorldY = HitResult.Location.Y;

            bool bSnapToCorners = true;
            if (BlockDataTable)
            {
                TArray<FName> RowNames = BlockDataTable->GetRowNames();
                if (RowNames.IsValidIndex(CurrentBuildRowIndex))
                {
                    FName RowName = RowNames[CurrentBuildRowIndex];
                    FBlockData* BlockData = BlockDataTable->FindRow<FBlockData>(RowName, TEXT(""));
                    if (BlockData)
                    {
                        bSnapToCorners = BlockData->bSnapToCorners;
                    }
                }
            }

            float SnappedX, SnappedY;

            if (bSnapToCorners)
            {
                UE_LOG(LogTemp, Warning, TEXT("[%s] Snap to corners modu - köşeler test ediliyor"),
                    bIsServer ? TEXT("SERVER") : TEXT("CLIENT"));

                int32 LowerGridX = FMath::FloorToInt(WorldX / EffectiveBlockSize);
                int32 UpperGridX = LowerGridX + 1;
                int32 LowerGridY = FMath::FloorToInt(WorldY / EffectiveBlockSize);
                int32 UpperGridY = LowerGridY + 1;

                TArray<FVector2D> CornerPositions = {
                    FVector2D(LowerGridX * EffectiveBlockSize, LowerGridY * EffectiveBlockSize),
                    FVector2D(UpperGridX * EffectiveBlockSize, LowerGridY * EffectiveBlockSize),
                    FVector2D(LowerGridX * EffectiveBlockSize, UpperGridY * EffectiveBlockSize),
                    FVector2D(UpperGridX * EffectiveBlockSize, UpperGridY * EffectiveBlockSize)
                };

                FVector2D HitPos(WorldX, WorldY);
                float BestDistance = FLT_MAX;
                bool bFoundValidCorner = false;
                FVector2D BestCorner = FVector2D::ZeroVector;
                int32 ValidCornerCount = 0;

                for (int32 i = 0; i < CornerPositions.Num(); i++)
                {
                    const FVector2D& Corner = CornerPositions[i];
                    FVector CornerWorldPos = FVector(Corner.X, Corner.Y, HitResult.Location.Z);

                    UE_LOG(LogTemp, Warning, TEXT("[%s] >>> Köşe %d test ediliyor: (%f, %f)"),
                        bIsServer ? TEXT("SERVER") : TEXT("CLIENT"), i, Corner.X, Corner.Y);

                    bool bHasFunctionalAtCorner = IsFunctionalBlockAt(CornerWorldPos);

                    UE_LOG(LogTemp, Warning, TEXT("[%s] >>> Köşe %d functional block kontrolü: %s"),
                        bIsServer ? TEXT("SERVER") : TEXT("CLIENT"), i,
                        bHasFunctionalAtCorner ? TEXT("ENGELLENDİ (FUNCTIONAL VAR)") : TEXT("GEÇERLİ (BOŞ)"));

                    if (bHasFunctionalAtCorner)
                    {
                        UE_LOG(LogTemp, Warning, TEXT("[%s] >>> Köşe %d ATLANDI - Functional block var"),
                            bIsServer ? TEXT("SERVER") : TEXT("CLIENT"), i);
                        continue;
                    }

                    ValidCornerCount++;
                    float Distance = FVector2D::DistSquared(HitPos, Corner);
                    if (Distance < BestDistance)
                    {
                        BestDistance = Distance;
                        BestCorner = Corner;
                        bFoundValidCorner = true;
                        UE_LOG(LogTemp, Warning, TEXT("[%s] >>> YENİ EN İYİ KÖŞE: %d (%f, %f) mesafe: %f"),
                            bIsServer ? TEXT("SERVER") : TEXT("CLIENT"), i, Corner.X, Corner.Y, FMath::Sqrt(Distance));
                    }
                }

                UE_LOG(LogTemp, Warning, TEXT("[%s] Köşe tarama tamamlandı - Geçerli köşe sayısı: %d / 4"),
                    bIsServer ? TEXT("SERVER") : TEXT("CLIENT"), ValidCornerCount);

                if (!bFoundValidCorner)
                {
                    UE_LOG(LogTemp, Error, TEXT("[%s] *** HİÇBİR GEÇERLİ KÖŞE BULUNAMADI! ***"),
                        bIsServer ? TEXT("SERVER") : TEXT("CLIENT"));
                    bHasValidPlacement = false;
                    GhostBlockMesh->SetVisibility(false);
                    return;
                }

                SnappedX = BestCorner.X;
                SnappedY = BestCorner.Y;

                UE_LOG(LogTemp, Warning, TEXT("[%s] EN İYİ KÖŞE SEÇİLDİ: (%f, %f)"),
                    bIsServer ? TEXT("SERVER") : TEXT("CLIENT"), SnappedX, SnappedY);
            }
            else
            {
                UE_LOG(LogTemp, Warning, TEXT("[%s] Merkeze snap modu"),
                    bIsServer ? TEXT("SERVER") : TEXT("CLIENT"));

                SnappedX = FMath::FloorToInt(WorldX / EffectiveBlockSize) * EffectiveBlockSize + (EffectiveBlockSize / 2.0f);
                SnappedY = FMath::FloorToInt(WorldY / EffectiveBlockSize) * EffectiveBlockSize + (EffectiveBlockSize / 2.0f);

                FVector CenterPos = FVector(SnappedX, SnappedY, HitResult.Location.Z);
                bool bCenterBlocked = IsFunctionalBlockAt(CenterPos);

                UE_LOG(LogTemp, Warning, TEXT("[%s] Merkez kontrolü (%f, %f): %s"),
                    bIsServer ? TEXT("SERVER") : TEXT("CLIENT"), SnappedX, SnappedY,
                    bCenterBlocked ? TEXT("ENGELLENDİ") : TEXT("GEÇERLİ"));

                if (bCenterBlocked)
                {
                    bHasValidPlacement = false;
                    GhostBlockMesh->SetVisibility(false);
                    return;
                }
            }

            FVector SecondTraceStart = FVector(SnappedX, SnappedY, HitResult.Location.Z + EffectiveBlockSize * 2);
            FVector SecondTraceEnd = FVector(SnappedX, SnappedY, HitResult.Location.Z - EffectiveBlockSize * 10);

            FHitResult GroundHitResult;
            bool bFoundGround = GetWorld()->LineTraceSingleByChannel(GroundHitResult, SecondTraceStart, SecondTraceEnd, ECC_Visibility, QueryParams);

            if (bFoundGround)
            {
                float SnappedZ = GroundHitResult.Location.Z;

                float ZOffset = 0.0f;
                if (BlockDataTable)
                {
                    TArray<FName> RowNames = BlockDataTable->GetRowNames();
                    if (RowNames.IsValidIndex(CurrentBuildRowIndex))
                    {
                        FName RowName = RowNames[CurrentBuildRowIndex];
                        FBlockData* BlockData = BlockDataTable->FindRow<FBlockData>(RowName, TEXT(""));
                        if (BlockData)
                        {
                            ZOffset = BlockData->ZOffset;
                        }
                    }
                }

                FVector ActualLocation = FVector(SnappedX, SnappedY, SnappedZ);
                GhostBlockLocation = ActualLocation;

                bool bFinalCheck = IsFunctionalBlockAt(ActualLocation);

                UE_LOG(LogTemp, Warning, TEXT("[%s] *** FINAL KONTROL *** (%f, %f, %f): %s"),
                    bIsServer ? TEXT("SERVER") : TEXT("CLIENT"), SnappedX, SnappedY, SnappedZ,
                    bFinalCheck ? TEXT("ENGELLENDİ") : TEXT("GEÇERLİ"));

                if (bFinalCheck)
                {
                    bHasValidPlacement = false;
                    GhostBlockMesh->SetVisibility(false);
                    return;
                }

                FVector VisualLocation = FVector(SnappedX, SnappedY, SnappedZ + ZOffset);
                GhostBlockMesh->SetWorldLocation(VisualLocation);

                if (!bCanCurrentBlockRotate)
                {
                    GhostBlockMesh->SetWorldRotation(FRotator::ZeroRotator);
                }

                bHasValidPlacement = CanPlaceBlockAt(ActualLocation, CurrentBlockType);

                if (bHasValidPlacement)
                {
                    if (ValidPlacementMaterial)
                        GhostBlockMesh->SetMaterial(0, ValidPlacementMaterial);
                }
                else
                {
                    if (InvalidPlacementMaterial)
                        GhostBlockMesh->SetMaterial(0, InvalidPlacementMaterial);
                }

                GhostBlockMesh->SetVisibility(true);

                UE_LOG(LogTemp, Warning, TEXT("[%s] ===== GHOST VISIBLE ===== Geçerli: %s"),
                    bIsServer ? TEXT("SERVER") : TEXT("CLIENT"),
                    bHasValidPlacement ? TEXT("YES") : TEXT("NO"));
            }
            else
            {
                UE_LOG(LogTemp, Warning, TEXT("[%s] Zemin bulunamadı - ghost gizleniyor"),
                    bIsServer ? TEXT("SERVER") : TEXT("CLIENT"));
                bHasValidPlacement = false;
                GhostBlockMesh->SetVisibility(false);
            }
        }
        else
        {
            UE_LOG(LogTemp, Warning, TEXT("[%s] ===== NORMAL BLOCK GHOST GÜNCELLE ====="),
                bIsServer ? TEXT("SERVER") : TEXT("CLIENT"));

            FVector PlacementLocation;
            FVector SurfaceNormal;
            bHasValidPlacement = FindPlacementSurface(PlacementLocation, SurfaceNormal);

            if (bHasValidPlacement)
            {
                float SnappedX = FMath::FloorToInt(PlacementLocation.X / EffectiveBlockSize) * EffectiveBlockSize + (EffectiveBlockSize / 2.0f);
                float SnappedY = FMath::FloorToInt(PlacementLocation.Y / EffectiveBlockSize) * EffectiveBlockSize + (EffectiveBlockSize / 2.0f);
                float SnappedZ = FMath::FloorToInt(PlacementLocation.Z / EffectiveBlockSize) * EffectiveBlockSize + (EffectiveBlockSize / 2.0f);

                FVector SnappedLocation = FVector(SnappedX, SnappedY, SnappedZ);

                bool bBlockedByFunctional = IsFunctionalBlockAt(SnappedLocation);

                UE_LOG(LogTemp, Warning, TEXT("[%s] Normal blok lokasyon (%f, %f, %f) - Functional engel: %s"),
                    bIsServer ? TEXT("SERVER") : TEXT("CLIENT"), SnappedX, SnappedY, SnappedZ,
                    bBlockedByFunctional ? TEXT("YES - ENGELLENDİ") : TEXT("NO - GEÇERLİ"));

                if (bBlockedByFunctional)
                {
                    UE_LOG(LogTemp, Warning, TEXT("[%s] *** NORMAL BLOK FUNCTIONAL BLOCK TARAFINDAN ENGELLENDİ ***"),
                        bIsServer ? TEXT("SERVER") : TEXT("CLIENT"));
                    bHasValidPlacement = false;
                    GhostBlockMesh->SetVisibility(false);
                    return;
                }

                GhostBlockLocation = SnappedLocation;
                GhostBlockMesh->SetWorldLocation(SnappedLocation);
                GhostBlockMesh->SetWorldRotation(FRotator::ZeroRotator);

                bHasValidPlacement = CanPlaceBlockAt(SnappedLocation, CurrentBlockType);

                if (bHasValidPlacement)
                {
                    if (ValidPlacementMaterial)
                        GhostBlockMesh->SetMaterial(0, ValidPlacementMaterial);
                }
                else
                {
                    if (InvalidPlacementMaterial)
                        GhostBlockMesh->SetMaterial(0, InvalidPlacementMaterial);
                }

                GhostBlockMesh->SetVisibility(true);
                UE_LOG(LogTemp, Warning, TEXT("[%s] Normal blok ghost gösterildi"),
                    bIsServer ? TEXT("SERVER") : TEXT("CLIENT"));
            }
            else
            {
                GhostBlockMesh->SetVisibility(false);
            }
        }
    }
    else
    {
        bHasValidPlacement = false;
        GhostBlockMesh->SetVisibility(false);
    }
}

bool UBuildSystem::FindPlacementSurface(FVector& OutLocation, FVector& OutNormal)
{
    FVector ViewLocation;
    FRotator ViewRotation;
    GetPlayerViewPoint(ViewLocation, ViewRotation);

    FVector TraceStart = ViewLocation;
    FVector TraceEnd = ViewLocation + ViewRotation.Vector() * BuildDistance;
    FHitResult HitResult;

    FCollisionQueryParams QueryParams;
    QueryParams.AddIgnoredActor(GetOwner());

    if (GetWorld()->LineTraceSingleByChannel(HitResult, TraceStart, TraceEnd, ECC_Visibility, QueryParams))
    {
        OutLocation = HitResult.Location;
        OutNormal = HitResult.Normal;

        LookDirection = ViewRotation.Vector();

        float EffectiveBlockSize = MapGenerator->BlockSize + MapGenerator->BlockSpacing;
        OutLocation += OutNormal * (EffectiveBlockSize / 2.0f);

        return true;
    }

    return false;
}

FName UBuildSystem::GetCurrentItemName() const
{
    return CurrentItemName;
}

FName UBuildSystem::GetItemNameForBlockType(EBlockType BlockType) const
{
    if (!BlockDataTable)
        return NAME_None;

    FString BlockTypeStr = UEnum::GetValueAsString(BlockType).Replace(TEXT("EBlockType::"), TEXT(""));
    FBlockData* BlockData = BlockDataTable->FindRow<FBlockData>(FName(*BlockTypeStr), TEXT(""));

    if (BlockData)
        return BlockData->ItemName;

    return NAME_None;
}

FName UBuildSystem::GetItemNameForRowIndex(int32 RowIndex) const
{
    if (!BlockDataTable)
        return NAME_None;

    TArray<FName> RowNames = BlockDataTable->GetRowNames();

    if (!RowNames.IsValidIndex(RowIndex))
        return NAME_None;

    FName RowName = RowNames[RowIndex];
    FBlockData* BlockData = BlockDataTable->FindRow<FBlockData>(RowName, TEXT(""));

    if (BlockData)
        return BlockData->ItemName;

    return NAME_None;
}

bool UBuildSystem::IsFunctionalBlockNearby(const FVector& Location, float Radius) const
{
    return false;
}

int32 UBuildSystem::GetRowIndexByItemName(const FName& SearchItemName) const
{
    if (!BlockDataTable)
        return -1;

    TArray<FName> RowNames = BlockDataTable->GetRowNames();

    for (int32 i = 0; i < RowNames.Num(); i++)
    {
        FBlockData* BlockData = BlockDataTable->FindRow<FBlockData>(RowNames[i], TEXT(""));
        if (BlockData && BlockData->ItemName == SearchItemName)
        {
            return i;
        }
    }

    return -1;
}

void UBuildSystem::InitializeDebugSystem()
{
    if (!DebugManager)
    {
        DebugManager = UDebugManager::FindDebugManager(GetWorld());
    }

    if (DebugManager)
    {
        LogDebugMessage(EDebugCategory::BuildSystem, TEXT("Debug system initialized for BuildSystem"));
    }
}

// Helper functions
void UBuildSystem::DrawDebugSphereIfEnabled(EDebugCategory Category, const FVector& Center, float Radius, const FColor& Color, bool bPersistent)
{
    if (DebugManager)
    {
        DebugManager->DrawDebugSphereIfEnabled(Category, Center, Radius, Color, bPersistent);
    }
}

void UBuildSystem::DrawDebugLineIfEnabled(EDebugCategory Category, const FVector& Start, const FVector& End, const FColor& Color, bool bPersistent)
{
    if (DebugManager)
    {
        DebugManager->DrawDebugLineIfEnabled(Category, Start, End, Color, bPersistent);
    }
}

void UBuildSystem::DrawDebugBoxIfEnabled(EDebugCategory Category, const FVector& Center, const FVector& Extent, const FColor& Color, bool bPersistent)
{
    if (DebugManager)
    {
        DebugManager->DrawDebugBoxIfEnabled(Category, Center, Extent, Color, bPersistent);
    }
}

void UBuildSystem::LogDebugMessage(EDebugCategory Category, const FString& Message, bool bWarning)
{
    if (DebugManager)
    {
        DebugManager->PrintDebugLog(Category, Message, bWarning);
    }
}

void UBuildSystem::ReinitializeAllDebugSystems()
{
    UE_LOG(LogTemp, Warning, TEXT("=== REINITIALIZING ALL DEBUG SYSTEMS ==="));

    UWorld* World = GetWorld();
    if (!World) return;

    // 1. BuildSystem debug'ı reinitialize et
    DebugManager = nullptr;
    DebugManager = UDebugManager::FindDebugManager(World);

    if (DebugManager)
    {
        UE_LOG(LogTemp, Warning, TEXT("BuildSystem DebugManager reinitialized"));
    }

    // 2. RandomMapGenerator debug'ı reinitialize et
    if (MapGenerator.IsValid())
    {
        ARandomMapGenerator* MapGen = MapGenerator.Get();
        MapGen->DebugManager = nullptr;
        MapGen->DebugManager = UDebugManager::FindDebugManager(World);

        if (MapGen->DebugManager)
        {
            UE_LOG(LogTemp, Warning, TEXT("MapGenerator DebugManager reinitialized"));
        }
    }

    // 3. Tüm dünyadaki MapGenerator'ları bul ve reinitialize et
    TArray<AActor*> FoundMapGenerators;
    UGameplayStatics::GetAllActorsOfClass(World, ARandomMapGenerator::StaticClass(), FoundMapGenerators);

    for (AActor* Actor : FoundMapGenerators)
    {
        ARandomMapGenerator* MapGenActor = Cast<ARandomMapGenerator>(Actor);
        if (MapGenActor)
        {
            MapGenActor->DebugManager = nullptr;
            MapGenActor->DebugManager = UDebugManager::FindDebugManager(World);

            if (MapGenActor->DebugManager)
            {
                UE_LOG(LogTemp, Warning, TEXT("Additional MapGenerator DebugManager reinitialized: %s"), *MapGenActor->GetName());
            }
        }
    }

    UE_LOG(LogTemp, Warning, TEXT("=== ALL DEBUG SYSTEMS REINITIALIZED ==="));

    // Test çizimi
    if (DebugManager)
    {
        FVector TestLoc = GetOwner()->GetActorLocation();
        DrawDebugSphere(World, TestLoc, 100.0f, 12, FColor::Green, false, 5.0f);
        UE_LOG(LogTemp, Warning, TEXT("Test debug sphere drawn - all systems should work now"));
    }
}

bool UBuildSystem::IsLocationBlockedByInvisibleWall(const FVector& Location)
{
    if (!MapGenerator.IsValid())
        return false;

    EBlockType BlockType = MapGenerator->GetBlockTypeAtPosition(Location);
    return (BlockType == EBlockType::InvisibleWall);
}