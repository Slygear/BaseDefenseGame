// ARandomMapGenerator.cpp - Complete implementation with CHUNK-BASED ISM System and Cave Spawn Integration
#include "ARandomMapGenerator.h"
#include "Net/UnrealNetwork.h"
#include "Engine/World.h"
#include "TimerManager.h"
#include "Kismet/GameplayStatics.h"
#include "Math/UnrealMathUtility.h"

ARandomMapGenerator::ARandomMapGenerator()
{
    PrimaryActorTick.bCanEverTick = true;
    bReplicates = true;
    bAlwaysRelevant = true;
    bIsGeneratingWorld = false;
    bHasGeneratedWorld = false;
    bWorldGenerationComplete = false;
    ChunksToGenerate = 0;
    ChunksGenerated = 0;
    bServerGenerationComplete = false;
    bClientGenerationComplete = false;

    // Enhanced Cave System default values
    CavesPerEdge = 1;
    CaveRockyFormationRadius = 6;
    MaxExtraRockHeight = 6;
    RockFormationDensity = 0.4f;
    bCreateRockyFormations = true;
    bNaturalCaveTunnels = true;
    CaveTunnelDeviation = 2;
    CaveHeightVariation = 2;
    CaveFloorVariation = 1;

    // Cave spawn system default values
    bSpawnEnemiesInCaves = true;
    CaveSpawnDepthRatio = 0.5f;

    // Create root component
    RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("RootComponent"));
    // Initialize RandomStream
    RandomStream = FRandomStream(0);
}

void ARandomMapGenerator::BeginPlay()
{
    Super::BeginPlay();
    // Initialize ISMs for each block type (now chunk-based)
    InitializeBlockISMs();
    // Initialize debug system
    InitializeDebugSystem();
}

void ARandomMapGenerator::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);
}

void ARandomMapGenerator::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    // Replicate generation settings - critically important for seed-based generation
    DOREPLIFETIME(ARandomMapGenerator, WorldSizeInChunks);
    DOREPLIFETIME(ARandomMapGenerator, ChunkSize);
    DOREPLIFETIME(ARandomMapGenerator, ChunkHeight);
    DOREPLIFETIME(ARandomMapGenerator, Seed);
    DOREPLIFETIME(ARandomMapGenerator, MapFlatness);
    DOREPLIFETIME(ARandomMapGenerator, TreeDensity);
    DOREPLIFETIME(ARandomMapGenerator, BaseHeight);
    DOREPLIFETIME(ARandomMapGenerator, HeightVariation);
    DOREPLIFETIME(ARandomMapGenerator, NoiseScale);
    DOREPLIFETIME(ARandomMapGenerator, BlockSize);
    DOREPLIFETIME(ARandomMapGenerator, BlockSpacing);
    DOREPLIFETIME(ARandomMapGenerator, SpawnedBaseCore);
    // Only replicate completion flag, not all blocks
    DOREPLIFETIME(ARandomMapGenerator, bWorldGenerationComplete);
}

void ARandomMapGenerator::OnRep_GenerationSettings()
{
    UE_LOG(LogTemp, Warning, TEXT("OnRep_GenerationSettings - Seed: %d"), Seed);
    // When settings are replicated to client, and world generation is complete on server
    // We can safely generate the world on client using the same seed
    if (bWorldGenerationComplete && !bIsGeneratingWorld && !bHasGeneratedWorld)
    {
        ClientGenerateWorld();
    }
}

void ARandomMapGenerator::OnRep_WorldGenerationComplete()
{
    UE_LOG(LogTemp, Warning, TEXT("OnRep_WorldGenerationComplete - Seed: %d"), Seed);
    // Server has completed world generation, now client can start
    if (!HasAuthority() && !bHasGeneratedWorld && !bIsGeneratingWorld)
    {
        ClientGenerateWorld();
    }
}

// *** UPDATED: CHUNK-BASED ISM INITIALIZATION ***
void ARandomMapGenerator::InitializeBlockISMs()
{
    // Clear any existing chunk ISM system
    for (auto& ChunkPair : ChunkISMSystem)
    {
        FChunkISMData& ChunkData = ChunkPair.Value;
        for (auto& ISMPair : ChunkData.ChunkISMs)
        {
            if (ISMPair.Value)
            {
                ISMPair.Value->DestroyComponent();
            }
        }
    }
    ChunkISMSystem.Empty();

    UE_LOG(LogTemp, Warning, TEXT("Chunk-based ISM system initialized (chunks will be created on-demand)"));
}

// *** NEW: CHUNK ISM INITIALIZATION ***
void ARandomMapGenerator::InitializeChunkISMs(const FChunkCoord& ChunkCoord)
{
    if (ChunkISMSystem.Contains(ChunkCoord))
    {
        UE_LOG(LogTemp, Warning, TEXT("Chunk ISMs already exist for chunk (%d,%d)"), ChunkCoord.X, ChunkCoord.Y);
        return;
    }

    FChunkISMData NewChunkData;

    // Her blok tipi için ISM oluştur
    for (int32 TypeIdx = 1; TypeIdx < static_cast<int32>(EBlockType::MAX); TypeIdx++)
    {
        EBlockType BlockType = static_cast<EBlockType>(TypeIdx);
        if (BlockType == EBlockType::Air) continue;

        // ISM component oluştur
        FString ComponentName = FString::Printf(TEXT("ChunkISM_%d_%d_%s"),
            ChunkCoord.X, ChunkCoord.Y,
            *UEnum::GetValueAsString(BlockType).Replace(TEXT("EBlockType::"), TEXT("")));

        UHierarchicalInstancedStaticMeshComponent* ChunkISM = NewObject<UHierarchicalInstancedStaticMeshComponent>(this, FName(*ComponentName));
        ChunkISM->SetupAttachment(RootComponent);
        ChunkISM->RegisterComponent();

        // ISM ayarları
        if (BlockType == EBlockType::InvisibleWall)
        {
            ChunkISM->SetCollisionProfileName(TEXT("BlockAll"));
            ChunkISM->SetVisibility(false);
            ChunkISM->SetHiddenInGame(true);
            ChunkISM->SetGenerateOverlapEvents(true);
            ChunkISM->SetCanEverAffectNavigation(true);
            ChunkISM->bCastDynamicShadow = false;
        }
        else
        {
            ChunkISM->SetCollisionProfileName(TEXT("BlockAll"));
            ChunkISM->SetGenerateOverlapEvents(true);
            ChunkISM->SetCanEverAffectNavigation(true);
            ChunkISM->bCastDynamicShadow = true;
        }

        // Mesh ve material ayarla
        if (BlockDataTable)
        {
            FString BlockTypeStr = UEnum::GetValueAsString(BlockType).Replace(TEXT("EBlockType::"), TEXT(""));
            FBlockData* BlockDataRow = BlockDataTable->FindRow<FBlockData>(FName(*BlockTypeStr), TEXT(""));
            if (BlockDataRow)
            {
                ChunkISM->SetStaticMesh(BlockDataRow->BlockMesh);
                if (BlockDataRow->BlockMaterial)
                {
                    ChunkISM->SetMaterial(0, BlockDataRow->BlockMaterial);
                }
            }
        }

        // Chunk data'ya ekle
        NewChunkData.ChunkISMs.Add(BlockType, ChunkISM);
        NewChunkData.InstanceCounts.Add(BlockType, 0);

        UE_LOG(LogTemp, VeryVerbose, TEXT("Created chunk ISM for (%d,%d) type %s"),
            ChunkCoord.X, ChunkCoord.Y, *UEnum::GetValueAsString(BlockType));
    }

    ChunkISMSystem.Add(ChunkCoord, NewChunkData);
    UE_LOG(LogTemp, Display, TEXT("Chunk ISM system initialized for chunk (%d,%d)"), ChunkCoord.X, ChunkCoord.Y);
}

UInstancedStaticMeshComponent* ARandomMapGenerator::GetChunkISM(const FChunkCoord& ChunkCoord, EBlockType BlockType)
{
    if (!ChunkISMSystem.Contains(ChunkCoord))
    {
        return nullptr;
    }

    return ChunkISMSystem[ChunkCoord].ChunkISMs.FindRef(BlockType);
}

void ARandomMapGenerator::GenerateWorld()
{
    // Force regeneration
    bHasGeneratedWorld = false;
    UE_LOG(LogTemp, Warning, TEXT("GenerateWorld called - Seed: %d"), Seed);
    if (HasAuthority())
    {
        ServerGenerateWorld();
    }
    else
    {
        // If we're on client, we'll wait for the server to complete generation first
        // Client generation will be triggered by OnRep_WorldGenerationComplete
        UE_LOG(LogTemp, Display, TEXT("Client waiting for server to complete generation"));
    }
}

void ARandomMapGenerator::SetNewSeed(int32 NewSeed)
{
    // Only server should change the seed
    if (HasAuthority())
    {
        // Eğer aynı seed girilirse, farklı bir seed oluştur
        if (NewSeed == Seed)
        {
            // Basit bir time-based yöntem ekleyelim (daha rastgele bir seed için)
            NewSeed = FMath::Abs(FDateTime::Now().GetTicks() % INT32_MAX);
            UE_LOG(LogTemp, Warning, TEXT("Aynı seed değeri girildi, yeni rastgele seed oluşturuldu: %d"), NewSeed);
        }

        bServerGenerationComplete = false;
        bClientGenerationComplete = false;
        // Eski seed'i logla
        UE_LOG(LogTemp, Warning, TEXT("Seed değiştiriliyor: %d -> %d"), Seed, NewSeed);
        Seed = NewSeed;
        bHasGeneratedWorld = false;
        bWorldGenerationComplete = false;
        // Event flag'ini sıfırla - YENİ EKLEME
        bEventAlreadyBroadcast = false;
        // Random Stream'i hemen başlat (consistency için)
        RandomStream.Initialize(Seed);
        FMath::RandInit(Seed);
        // İşlenmiş blokları temizle
        ProcessedDestroyedBlocks.Empty();
        DestroyedBlocksProcessed.Empty();
        // Force world regeneration with new seed
        ServerGenerateWorld();
    }
    else
    {
        UE_LOG(LogTemp, Warning, TEXT("SetNewSeed called on client - this should only be called on server"));
    }
}

void ARandomMapGenerator::ClearGeneratorState()
{
    // Clear all existing data structures for clean regeneration
    BlocksData.Empty();
    ChunksInfo.Empty();
    BlockDamageData.Empty();
    DestroyedBlocksProcessed.Empty();
    ProcessedDestroyedBlocks.Empty();

    // *** NEW: Clear cave locations ***
    CaveLocations.Empty();

    // *** UPDATED: Clear chunk ISM system ***
    for (auto& ChunkPair : ChunkISMSystem)
    {
        FChunkISMData& ChunkData = ChunkPair.Value;
        for (auto& ISMPair : ChunkData.ChunkISMs)
        {
            if (ISMPair.Value)
            {
                ISMPair.Value->ClearInstances();
                ISMPair.Value->DestroyComponent();
            }
        }
    }
    ChunkISMSystem.Empty();

    bServerGenerationComplete = false;
    bClientGenerationComplete = false;

    UE_LOG(LogTemp, Display, TEXT("Generator state cleared - chunk ISM system and cave locations reset"));
}

bool ARandomMapGenerator::ServerGenerateWorld_Validate()
{
    return true;
}

void ARandomMapGenerator::ServerGenerateWorld_Implementation()
{
    // Only server can initiate world generation
    if (!HasAuthority() || bIsGeneratingWorld)
        return;

    LogDebugMessage(EDebugCategory::WorldGeneration,
        FString::Printf(TEXT("Server: Starting world generation with seed: %d"), Seed));

    // Clear existing world and state
    ClearGeneratorState();

    // Initialize generation
    bIsGeneratingWorld = true;
    bHasGeneratedWorld = false;
    bWorldGenerationComplete = false;
    ChunksToGenerate = WorldSizeInChunks * WorldSizeInChunks;
    ChunksGenerated = 0;

    // Seed'i başlatmanın birkaç farklı yolu (daha sağlam olması için)
    FMath::RandInit(Seed);
    RandomStream.Initialize(Seed);

    // Seed'in etki ettiğini doğrulamak için örnek yükseklik değerlerini kaydet - UPDATED
    LogDebugMessage(EDebugCategory::WorldGeneration,
        FString::Printf(TEXT("Seed verification - terrain height samples for seed %d:"), Seed));

    for (int32 i = 0; i < 10; i++)
    {
        int32 Height = GetTerrainHeight(i, i);
        LogDebugMessage(EDebugCategory::WorldGeneration,
            FString::Printf(TEXT("Sample terrain height at (%d,%d): %d"), i, i, Height));
    }

    UE_LOG(LogTemp, Warning, TEXT("SERVER: 1. Normal chunk generation başlıyor..."));

    // Generate all chunks
    for (int32 X = 0; X < WorldSizeInChunks; X++)
    {
        for (int32 Y = 0; Y < WorldSizeInChunks; Y++)
        {
            FChunkCoord ChunkCoord(X, Y);
            GenerateChunk(ChunkCoord);

            // Update progress
            ChunksGenerated++;
            float Progress = static_cast<float>(ChunksGenerated) / ChunksToGenerate;
            OnGenerationProgressUpdated.Broadcast(Progress);
        }
    }

    UE_LOG(LogTemp, Warning, TEXT("SERVER: 2. Base Core spawn ediliyor..."));
    // Base Core'u spawn et
    SpawnBaseCore();

    UE_LOG(LogTemp, Warning, TEXT("SERVER: 3. Spawn points oluşturuluyor..."));
    // Generate spawn points (Debug amaçlı) - SADECE DEBUG GÖRSELLEŞTİRME
    GenerateSpawnPoints();

    UE_LOG(LogTemp, Warning, TEXT("SERVER: 4. Mountain Border System başlatılıyor..."));
    // *** YENİ MOUNTAIN BORDER SİSTEMİ ***
    if (bCreateMountainBorders)
    {
        GenerateMountainBorderSystem();
    }

    UE_LOG(LogTemp, Warning, TEXT("SERVER: 5. AI Debug walls kontrol ediliyor..."));
    // AI Debug Modunda duvarlar oluştur
    if (bAIDebugMode)
    {
        GenerateDebugWalls();
    }

    UE_LOG(LogTemp, Warning, TEXT("SERVER: 6. All chunks generated with chunk-based ISM system!"));

    bIsGeneratingWorld = false;
    bHasGeneratedWorld = true;
    bWorldGenerationComplete = true;  // This will be replicated to clients

    // Notify all clients that generation is complete
    MulticastGenerationComplete();

    // Server generation complete
    bServerGenerationComplete = true;

    // Server-specific events
    OnServerWorldGenerationComplete.Broadcast();
    OnPlayerWorldGenerationComplete.Broadcast(true); // true = server

    // Genel event (backward compatibility için)
    if (!bEventAlreadyBroadcast)
    {
        bEventAlreadyBroadcast = true;
        OnWorldGenerationComplete.Broadcast();
        UE_LOG(LogTemp, Warning, TEXT("OnWorldGenerationComplete event broadcast from SERVER - ONLY ONCE"));
    }

    LogDebugMessage(EDebugCategory::WorldGeneration,
        FString::Printf(TEXT("Server: World generation complete. Generated %d chunks with Seed: %d"),
            ChunksGenerated, Seed));

    UE_LOG(LogTemp, Warning, TEXT("SERVER: *** WORLD GENERATION COMPLETE ***"));
}

void ARandomMapGenerator::ClientGenerateWorld()
{
    // Only clients should run this
    if (HasAuthority() || bIsGeneratingWorld || bHasGeneratedWorld)
        return;
    UE_LOG(LogTemp, Warning, TEXT("Client: Starting world generation with seed: %d"), Seed);
    // Clear existing world and state
    ClearGeneratorState();
    // Initialize generation
    bIsGeneratingWorld = true;
    ChunksToGenerate = WorldSizeInChunks * WorldSizeInChunks;
    ChunksGenerated = 0;
    // Set random seed - CRITICAL: must use the same seed as server
    FMath::RandInit(Seed);
    RandomStream.Initialize(Seed);
    // Log sample terrain heights to verify seed impact
    UE_LOG(LogTemp, Display, TEXT("Client-side seed verification - terrain height samples:"));
    for (int32 i = 0; i < 5; i++)
    {
        int32 Height = GetTerrainHeight(i, i);
        UE_LOG(LogTemp, Display, TEXT("Sample terrain height at (%d,%d): %d"), i, i, Height);
    }

    UE_LOG(LogTemp, Warning, TEXT("CLIENT: 1. Normal chunk generation başlıyor..."));
    // Generate all chunks - same deterministic algorithm as server
    for (int32 X = 0; X < WorldSizeInChunks; X++)
    {
        for (int32 Y = 0; Y < WorldSizeInChunks; Y++)
        {
            FChunkCoord ChunkCoord(X, Y);
            GenerateChunk(ChunkCoord);
            // Update progress
            ChunksGenerated++;
            float Progress = static_cast<float>(ChunksGenerated) / ChunksToGenerate;
            OnGenerationProgressUpdated.Broadcast(Progress);
        }
    }

    UE_LOG(LogTemp, Warning, TEXT("CLIENT: 2. AI Debug walls kontrol ediliyor..."));
    // AI Debug Modunda duvarlar oluştur - client da oluştursun
    if (bAIDebugMode)
    {
        GenerateDebugWalls();
    }

    UE_LOG(LogTemp, Warning, TEXT("CLIENT: 3. Mountain Border System başlatılıyor..."));
    // *** YENİ MOUNTAIN BORDER SİSTEMİ ***
    if (bCreateMountainBorders)
    {
        GenerateMountainBorderSystem();
    }

    UE_LOG(LogTemp, Warning, TEXT("CLIENT: 4. All chunks generated with chunk-based ISM system!"));

    bIsGeneratingWorld = false;
    bHasGeneratedWorld = true;

    // Client generation complete
    bClientGenerationComplete = true;

    // Client-specific events
    OnClientWorldGenerationComplete.Broadcast();
    OnPlayerWorldGenerationComplete.Broadcast(false); // false = client

    // Genel event (backward compatibility için)
    if (!bEventAlreadyBroadcast)
    {
        bEventAlreadyBroadcast = true;
        OnWorldGenerationComplete.Broadcast();
        UE_LOG(LogTemp, Warning, TEXT("OnWorldGenerationComplete event broadcast from CLIENT - ONLY ONCE"));
    }

    UE_LOG(LogTemp, Warning, TEXT("CLIENT: *** WORLD GENERATION COMPLETE ***"));
}

void ARandomMapGenerator::MulticastGenerationComplete_Implementation()
{
    // This will run on all clients
    if (!HasAuthority())
    {
        // Clients don't need to do anything here since they'll respond to 
        // OnRep_WorldGenerationComplete instead
        UE_LOG(LogTemp, Display, TEXT("Client received MulticastGenerationComplete notification"));
    }
}

// *** ENHANCED MOUNTAIN BORDER SYSTEM & CAVE IMPLEMENTATION ***

void ARandomMapGenerator::GenerateMountainBorderSystem()
{
    UE_LOG(LogTemp, Warning, TEXT("Mountain Border System oluşturuluyor..."));

    LogDebugMessage(EDebugCategory::WorldGeneration, TEXT("Starting Mountain Border System generation"));

    // Önce dağ sistemini oluştur
    for (int32 EdgeIndex = 0; EdgeIndex < 4; EdgeIndex++)
    {
        UE_LOG(LogTemp, Warning, TEXT("Mountain Edge %d oluşturuluyor..."), EdgeIndex);
        GenerateMountainRange(EdgeIndex);
    }

    // Sonra mağara sistemini oluştur (dağ bloklarını kaldırır)
    UE_LOG(LogTemp, Warning, TEXT("Enhanced Cave System oluşturuluyor..."));
    GenerateCaveSystem();

    LogDebugMessage(EDebugCategory::WorldGeneration, TEXT("Mountain Border System generation complete"));
    UE_LOG(LogTemp, Warning, TEXT("Mountain Border System tamamlandı!"));
}

void ARandomMapGenerator::GenerateMountainRange(int32 EdgeIndex)
{
    // Harita sınırlarını hesapla
    int32 MapMinX = 0;
    int32 MapMinY = 0;
    int32 MapMaxX = WorldSizeInChunks * ChunkSize - 1;
    int32 MapMaxY = WorldSizeInChunks * ChunkSize - 1;

    LogDebugMessage(EDebugCategory::WorldGeneration,
        FString::Printf(TEXT("Generating mountain range for edge %d - Map bounds: (%d,%d) to (%d,%d)"),
            EdgeIndex, MapMinX, MapMinY, MapMaxX, MapMaxY));

    int32 BlocksGenerated = 0;

    // Her kenar için dağ oluştur
    switch (EdgeIndex)
    {
    case 0: // Kuzey kenar
    {
        for (int32 X = MapMinX - MountainBorderWidth; X <= MapMaxX + MountainBorderWidth; X++)
        {
            for (int32 Depth = 0; Depth < MountainBorderWidth; Depth++)
            {
                int32 Y = MapMinY - Depth - 1;

                // Chunk koordinatlarını hesapla
                FChunkCoord ChunkCoord(FMath::FloorToInt(static_cast<float>(X) / ChunkSize),
                    FMath::FloorToInt(static_cast<float>(Y) / ChunkSize));

                int32 LocalX = X % ChunkSize;
                if (LocalX < 0) LocalX += ChunkSize;

                int32 LocalY = Y % ChunkSize;
                if (LocalY < 0) LocalY += ChunkSize;

                // Dağ yüksekliğini hesapla
                int32 MountainHeight = GetMountainHeight(X, Y, Depth);

                // Zemin referans yüksekliği
                int32 BaseGroundHeight = GetTerrainHeight(FMath::Clamp(X, MapMinX, MapMaxX), MapMinY);

                // Dağ bloklarını oluştur
                for (int32 Z = 0; Z < MountainHeight; Z++)
                {
                    FBlockPosition BlockPos(LocalX, LocalY, BaseGroundHeight + Z);
                    EBlockType BlockType = GetMountainBlockType(Z, MountainHeight, LocalX, LocalY);

                    SetBlockInternalWithoutReplication(ChunkCoord, BlockPos, BlockType);

                    // *** UPDATED: Direct chunk ISM addition instead of batch ***
                    UpdateBlockInstance(ChunkCoord, BlockPos, BlockType);

                    BlocksGenerated++;
                }
            }
        }
        break;
    }

    case 1: // Güney kenar
    {
        for (int32 X = MapMinX - MountainBorderWidth; X <= MapMaxX + MountainBorderWidth; X++)
        {
            for (int32 Depth = 0; Depth < MountainBorderWidth; Depth++)
            {
                int32 Y = MapMaxY + Depth + 1;

                FChunkCoord ChunkCoord(FMath::FloorToInt(static_cast<float>(X) / ChunkSize),
                    FMath::FloorToInt(static_cast<float>(Y) / ChunkSize));

                int32 LocalX = X % ChunkSize;
                if (LocalX < 0) LocalX += ChunkSize;

                int32 LocalY = Y % ChunkSize;
                if (LocalY < 0) LocalY += ChunkSize;

                int32 MountainHeight = GetMountainHeight(X, Y, Depth);
                int32 BaseGroundHeight = GetTerrainHeight(FMath::Clamp(X, MapMinX, MapMaxX), MapMaxY);

                for (int32 Z = 0; Z < MountainHeight; Z++)
                {
                    FBlockPosition BlockPos(LocalX, LocalY, BaseGroundHeight + Z);
                    EBlockType BlockType = GetMountainBlockType(Z, MountainHeight, LocalX, LocalY);

                    SetBlockInternalWithoutReplication(ChunkCoord, BlockPos, BlockType);
                    UpdateBlockInstance(ChunkCoord, BlockPos, BlockType);

                    BlocksGenerated++;
                }
            }
        }
        break;
    }

    case 2: // Batı kenar
    {
        for (int32 Y = MapMinY; Y <= MapMaxY; Y++)
        {
            for (int32 Depth = 0; Depth < MountainBorderWidth; Depth++)
            {
                int32 X = MapMinX - Depth - 1;

                FChunkCoord ChunkCoord(FMath::FloorToInt(static_cast<float>(X) / ChunkSize),
                    FMath::FloorToInt(static_cast<float>(Y) / ChunkSize));

                int32 LocalX = X % ChunkSize;
                if (LocalX < 0) LocalX += ChunkSize;

                int32 LocalY = Y % ChunkSize;
                if (LocalY < 0) LocalY += ChunkSize;

                int32 MountainHeight = GetMountainHeight(X, Y, Depth);
                int32 BaseGroundHeight = GetTerrainHeight(MapMinX, FMath::Clamp(Y, MapMinY, MapMaxY));

                for (int32 Z = 0; Z < MountainHeight; Z++)
                {
                    FBlockPosition BlockPos(LocalX, LocalY, BaseGroundHeight + Z);
                    EBlockType BlockType = GetMountainBlockType(Z, MountainHeight, LocalX, LocalY);

                    SetBlockInternalWithoutReplication(ChunkCoord, BlockPos, BlockType);
                    UpdateBlockInstance(ChunkCoord, BlockPos, BlockType);

                    BlocksGenerated++;
                }
            }
        }
        break;
    }

    case 3: // Doğu kenar
    {
        for (int32 Y = MapMinY; Y <= MapMaxY; Y++)
        {
            for (int32 Depth = 0; Depth < MountainBorderWidth; Depth++)
            {
                int32 X = MapMaxX + Depth + 1;

                FChunkCoord ChunkCoord(FMath::FloorToInt(static_cast<float>(X) / ChunkSize),
                    FMath::FloorToInt(static_cast<float>(Y) / ChunkSize));

                int32 LocalX = X % ChunkSize;
                if (LocalX < 0) LocalX += ChunkSize;

                int32 LocalY = Y % ChunkSize;
                if (LocalY < 0) LocalY += ChunkSize;

                int32 MountainHeight = GetMountainHeight(X, Y, Depth);
                int32 BaseGroundHeight = GetTerrainHeight(MapMaxX, FMath::Clamp(Y, MapMinY, MapMaxY));

                for (int32 Z = 0; Z < MountainHeight; Z++)
                {
                    FBlockPosition BlockPos(LocalX, LocalY, BaseGroundHeight + Z);
                    EBlockType BlockType = GetMountainBlockType(Z, MountainHeight, LocalX, LocalY);

                    SetBlockInternalWithoutReplication(ChunkCoord, BlockPos, BlockType);
                    UpdateBlockInstance(ChunkCoord, BlockPos, BlockType);

                    BlocksGenerated++;
                }
            }
        }
        break;
    }
    }

    UE_LOG(LogTemp, Warning, TEXT("Mountain Edge %d: %d blok oluşturuldu"), EdgeIndex, BlocksGenerated);
}

// *** ENHANCED CAVE SYSTEM IMPLEMENTATION ***

void ARandomMapGenerator::GenerateCaveSystem()
{
    // *** NEW: Clear old cave locations ***
    CaveLocations.Empty();

    LogDebugMessage(EDebugCategory::WorldGeneration, TEXT("Creating enhanced cave system with rocky formations"));

    // Her kenarda CavesPerEdge kadar mağara oluştur
    for (int32 EdgeIndex = 0; EdgeIndex < 4; EdgeIndex++)
    {
        if (CavesPerEdge == 1)
        {
            // Tek mağara - kenarın ortasında
            GenerateEnhancedCave(EdgeIndex, 0.5f);
        }
        else
        {
            // Çoklu mağaralar - kenarda eşit aralıklarla dağıt
            for (int32 CaveIndex = 0; CaveIndex < CavesPerEdge; CaveIndex++)
            {
                float EdgePosition = (CaveIndex + 1.0f) / (CavesPerEdge + 1.0f); // 0.2, 0.4, 0.6, 0.8 gibi
                GenerateEnhancedCave(EdgeIndex, EdgePosition);
            }
        }
    }

    // *** NEW: CAVE SYSTEM GENERATION REPORT ***
    UE_LOG(LogTemp, Warning, TEXT("=== CAVE SYSTEM GENERATION REPORT ==="));
    UE_LOG(LogTemp, Warning, TEXT("Caves per edge: %d"), CavesPerEdge);
    UE_LOG(LogTemp, Warning, TEXT("Total caves created: %d"), CaveLocations.Num());
    UE_LOG(LogTemp, Warning, TEXT("Cave spawn system active: %s"), bSpawnEnemiesInCaves ? TEXT("YES") : TEXT("NO"));
    UE_LOG(LogTemp, Warning, TEXT("Cave spawn depth ratio: %.2f"), CaveSpawnDepthRatio);

    for (int32 i = 0; i < CaveLocations.Num(); i++)
    {
        const FCaveLocation& Cave = CaveLocations[i];
        FString EdgeName = (Cave.EdgeIndex == 0) ? TEXT("NORTH") :
            (Cave.EdgeIndex == 1) ? TEXT("SOUTH") :
            (Cave.EdgeIndex == 2) ? TEXT("WEST") : TEXT("EAST");
        UE_LOG(LogTemp, Warning, TEXT("Cave %d: %s edge, spawn at %s"),
            i, *EdgeName, *Cave.CaveSpawnLocation.ToString());
    }

    UE_LOG(LogTemp, Warning, TEXT("Enhanced cave system complete: %d caves per edge, %d total spawn points"),
        CavesPerEdge, CaveLocations.Num());
}

void ARandomMapGenerator::GenerateEnhancedCave(int32 EdgeIndex, float EdgePosition)
{
    // Harita sınırlarını hesapla
    int32 MapMinX = 0;
    int32 MapMinY = 0;
    int32 MapMaxX = WorldSizeInChunks * ChunkSize - 1;
    int32 MapMaxY = WorldSizeInChunks * ChunkSize - 1;

    int32 CaveStartX, CaveStartY;
    int32 CaveDirectionX, CaveDirectionY;
    FString EdgeName;

    // Edge position'a göre mağara konumunu hesapla
    switch (EdgeIndex)
    {
    case 0: // Kuzey kenar
        CaveStartX = FMath::FloorToInt(MapMinX + (MapMaxX - MapMinX) * EdgePosition);
        CaveStartY = MapMinY - 1;
        CaveDirectionX = 0;
        CaveDirectionY = -1;
        EdgeName = TEXT("NORTH");
        break;

    case 1: // Güney kenar  
        CaveStartX = FMath::FloorToInt(MapMinX + (MapMaxX - MapMinX) * EdgePosition);
        CaveStartY = MapMaxY + 1;
        CaveDirectionX = 0;
        CaveDirectionY = 1;
        EdgeName = TEXT("SOUTH");
        break;

    case 2: // Batı kenar
        CaveStartX = MapMinX - 1;
        CaveStartY = FMath::FloorToInt(MapMinY + (MapMaxY - MapMinY) * EdgePosition);
        CaveDirectionX = -1;
        CaveDirectionY = 0;
        EdgeName = TEXT("WEST");
        break;

    case 3: // Doğu kenar
        CaveStartX = MapMaxX + 1;
        CaveStartY = FMath::FloorToInt(MapMinY + (MapMaxY - MapMinY) * EdgePosition);
        CaveDirectionX = 1;
        CaveDirectionY = 0;
        EdgeName = TEXT("EAST");
        break;

    default:
        return;
    }

    UE_LOG(LogTemp, Warning, TEXT("=== CREATING ENHANCED %s CAVE at (%d, %d) Position %.2f ==="),
        *EdgeName, CaveStartX, CaveStartY, EdgePosition);

    // Zemin referans yüksekliği
    int32 CaveBaseHeight = GetTerrainHeight(
        FMath::Clamp(CaveStartX, MapMinX, MapMaxX),
        FMath::Clamp(CaveStartY, MapMinY, MapMaxY)
    );

    // *** 1. ÖNCE MAĞARA GİRİŞİ ÇEVRESİNDE KAYALIK FORMASYONLAR OLUŞTUR ***
    GenerateCaveRockyFormations(CaveStartX, CaveStartY, CaveDirectionX, CaveDirectionY, CaveBaseHeight);

    // *** 2. MAĞARA TÜNELİNİ OLUŞTUR (ENGEBELİ VE DOĞAL) ***
    GenerateNaturalCaveTunnel(CaveStartX, CaveStartY, CaveDirectionX, CaveDirectionY, CaveBaseHeight);

    // *** 3. MAĞARA GİRİŞİNİ KAPAT (OPSIYONEL) ***
    if (bSealCaves)
    {
        SealCaveEntrance(CaveStartX, CaveStartY, CaveDirectionX, CaveDirectionY, CaveBaseHeight);
    }

    // *** NEW: MAĞARA SPAWN KONUMUNU HESAPLA VE KAYDET ***

    // Mağara girişi konumu (dünya koordinatları)
    FVector CaveEntranceWorldLocation = FVector(
        CaveStartX * (BlockSize + BlockSpacing) + (BlockSize / 2.0f),
        CaveStartY * (BlockSize + BlockSpacing) + (BlockSize / 2.0f),
        CaveBaseHeight * (BlockSize + BlockSpacing) + (BlockSize / 2.0f)
    );

    // Mağara içi spawn konumu hesapla (mağaranın içerisinde, belirli bir derinlikte)
    float SpawnDepth = CaveDepth * CaveSpawnDepthRatio; // Mağara derinliğinin yarısında
    int32 SpawnX = CaveStartX + (CaveDirectionX * FMath::RoundToInt(SpawnDepth));
    int32 SpawnY = CaveStartY + (CaveDirectionY * FMath::RoundToInt(SpawnDepth));

    FVector CaveSpawnWorldLocation = FVector(
        SpawnX * (BlockSize + BlockSpacing) + (BlockSize / 2.0f),
        SpawnY * (BlockSize + BlockSpacing) + (BlockSize / 2.0f),
        (CaveBaseHeight + 1) * (BlockSize + BlockSpacing) + (BlockSize / 2.0f) // Zeminin 1 blok üstü
    );

    // Mağara konumunu kaydet
    RegisterCaveLocation(CaveEntranceWorldLocation, CaveSpawnWorldLocation, EdgeIndex, EdgePosition);

    UE_LOG(LogTemp, Warning, TEXT("=== %s ENHANCED CAVE COMPLETE with SPAWN LOCATION ==="), *EdgeName);
}

void ARandomMapGenerator::GenerateCaveRockyFormations(int32 CaveX, int32 CaveY, int32 DirX, int32 DirY, int32 CaveBaseHeight)
{
    if (!bCreateRockyFormations)
    {
        UE_LOG(LogTemp, Display, TEXT("Rocky formations disabled, skipping"));
        return;
    }

    UE_LOG(LogTemp, Warning, TEXT("Creating rocky formations around cave entrance at (%d, %d)"), CaveX, CaveY);

    // Mağara çevresinde kayalık formasyonlar için alan
    int32 FormationRadius = CaveRockyFormationRadius;
    int32 BlocksPlaced = 0;

    // Seed'e dayalı noise offset'leri
    float NoiseOffsetX = Seed * 0.013f;
    float NoiseOffsetY = (Seed * 7919) * 0.009f;

    for (int32 OffsetX = -FormationRadius; OffsetX <= FormationRadius; OffsetX++)
    {
        for (int32 OffsetY = -FormationRadius; OffsetY <= FormationRadius; OffsetY++)
        {
            int32 WorldX = CaveX + OffsetX;
            int32 WorldY = CaveY + OffsetY;

            // Merkez mağara alanını atla (sonra tünel açılacak)
            if (abs(OffsetX) <= CaveWidth / 2 && abs(OffsetY) <= CaveWidth / 2)
                continue;

            // Chunk koordinatlarını hesapla
            FChunkCoord ChunkCoord(FMath::FloorToInt(static_cast<float>(WorldX) / ChunkSize),
                FMath::FloorToInt(static_cast<float>(WorldY) / ChunkSize));

            int32 LocalX = WorldX % ChunkSize;
            if (LocalX < 0) LocalX += ChunkSize;

            int32 LocalY = WorldY % ChunkSize;
            if (LocalY < 0) LocalY += ChunkSize;

            // Merkeze olan mesafeyi hesapla - FIX: int32'yi float'a çevir
            float DistanceFromCave = FMath::Sqrt(static_cast<float>(OffsetX * OffsetX + OffsetY * OffsetY));

            // Mesafe faktörü (merkeze yaklaştıkça daha yoğun)
            float DistanceFactor = FMath::Clamp(1.0f - (DistanceFromCave / FormationRadius), 0.0f, 1.0f);

            // Çoklu katmanlı Perlin noise (daha doğal formasyonlar için)
            float Noise1 = FMath::PerlinNoise2D(FVector2D(WorldX * 0.1f + NoiseOffsetX, WorldY * 0.1f + NoiseOffsetY));
            float Noise2 = FMath::PerlinNoise2D(FVector2D(WorldX * 0.3f + NoiseOffsetX, WorldY * 0.3f + NoiseOffsetY)) * 0.5f;
            float CombinedNoise = (Noise1 + Noise2) / 1.5f;

            // Kayalık oluşma olasılığı (RockFormationDensity ayarını kullan)
            float RockFormationChance = DistanceFactor * (CombinedNoise + 1.0f) * 0.5f * RockFormationDensity;

            // Minimum threshold
            if (RockFormationChance < 0.2f)
                continue;

            // Kayalık yüksekliğini hesapla (MaxExtraRockHeight ayarını kullan)
            int32 ExtraHeight = FMath::RoundToInt(RockFormationChance * MaxExtraRockHeight);
            int32 TotalHeight = CaveBaseHeight + ExtraHeight;

            // Kayalık bloklarını yerleştir
            for (int32 Z = CaveBaseHeight; Z < TotalHeight && Z < ChunkHeight; Z++)
            {
                FBlockPosition BlockPos(LocalX, LocalY, Z);

                // Blok tipini yüksekliğe göre seç
                EBlockType RockType;
                float HeightPercent = static_cast<float>(Z - CaveBaseHeight) / ExtraHeight;

                if (HeightPercent < 0.6f)
                {
                    RockType = EBlockType::Stone; // Alt kısımlar sert kaya
                }
                else if (HeightPercent < 0.9f)
                {
                    RockType = (CombinedNoise > 0.0f) ? EBlockType::Stone : EBlockType::Dirt;
                }
                else
                {
                    RockType = EBlockType::Dirt; // Üst kısımlar toprak
                }

                SetBlockInternalWithoutReplication(ChunkCoord, BlockPos, RockType);

                // *** UPDATED: Direct chunk ISM addition ***
                UpdateBlockInstance(ChunkCoord, BlockPos, RockType);

                BlocksPlaced++;
            }
        }
    }

    UE_LOG(LogTemp, Warning, TEXT("Rocky formations: %d blocks placed around cave"), BlocksPlaced);
}

void ARandomMapGenerator::GenerateNaturalCaveTunnel(int32 StartX, int32 StartY, int32 DirX, int32 DirY, int32 CaveBaseHeight)
{
    UE_LOG(LogTemp, Warning, TEXT("Creating natural cave tunnel from (%d, %d)"), StartX, StartY);

    int32 BlocksRemoved = 0;
    int32 BlocksPlaced = 0;

    // Seed'e dayalı tünel varyasyonu
    float TunnelNoiseOffsetX = Seed * 0.017f;
    float TunnelNoiseOffsetY = (Seed * 3571) * 0.019f;

    // Tünel boyunca ilerle
    for (int32 Depth = 0; Depth < CaveDepth; Depth++)
    {
        // Ana tünel konumu
        int32 CurrentX = StartX + (DirX * Depth);
        int32 CurrentY = StartY + (DirY * Depth);

        // Depth'e göre tünel genişliği değişimi (başlangıçta dar, ortada geniş)
        float DepthPercent = static_cast<float>(Depth) / CaveDepth;
        float WidthMultiplier = FMath::Sin(DepthPercent * PI); // 0'da ve sonda 0, ortada 1
        int32 CurrentWidth = FMath::Max(2, FMath::RoundToInt(CaveWidth * (0.5f + 0.5f * WidthMultiplier)));

        // Tünel eğimi/sapması (ayarlara göre) - doğal tüneller aktifse
        int32 Deviation = 0;
        if (bNaturalCaveTunnels && CaveTunnelDeviation > 0)
        {
            float DeviationNoise = FMath::PerlinNoise2D(FVector2D(Depth * 0.3f + TunnelNoiseOffsetX,
                Depth * 0.2f + TunnelNoiseOffsetY));
            Deviation = FMath::RoundToInt(DeviationNoise * CaveTunnelDeviation);
        }

        // Sapma yönünü belirle
        int32 DeviationX = 0, DeviationY = 0;
        if (DirX != 0) // Doğu/Batı tünelleri için Y ekseni sapması
        {
            DeviationY = Deviation;
        }
        else // Kuzey/Güney tünelleri için X ekseni sapması
        {
            DeviationX = Deviation;
        }

        // Ana tünel konumunu sapma ile güncelle
        CurrentX += DeviationX;
        CurrentY += DeviationY;

        // Tünel genişliği için döngü
        for (int32 WidthOffset = -CurrentWidth / 2; WidthOffset <= CurrentWidth / 2; WidthOffset++)
        {
            int32 TunnelX, TunnelY;

            // Yön bazlı genişlik hesaplama
            if (DirX != 0) // Doğu/Batı tünelleri
            {
                TunnelX = CurrentX;
                TunnelY = CurrentY + WidthOffset;
            }
            else // Kuzey/Güney tünelleri
            {
                TunnelX = CurrentX + WidthOffset;
                TunnelY = CurrentY;
            }

            // Chunk koordinatlarını hesapla
            FChunkCoord ChunkCoord(FMath::FloorToInt(static_cast<float>(TunnelX) / ChunkSize),
                FMath::FloorToInt(static_cast<float>(TunnelY) / ChunkSize));

            int32 LocalX = TunnelX % ChunkSize;
            if (LocalX < 0) LocalX += ChunkSize;

            int32 LocalY = TunnelY % ChunkSize;
            if (LocalY < 0) LocalY += ChunkSize;

            // Tünel yüksekliği varyasyonu (ayarlara göre)
            int32 ExtraHeight = 0;
            if (bNaturalCaveTunnels && CaveHeightVariation > 0)
            {
                float HeightNoise = FMath::PerlinNoise2D(FVector2D(TunnelX * 0.2f, TunnelY * 0.2f));
                ExtraHeight = FMath::RoundToInt(HeightNoise * CaveHeightVariation);
            }
            int32 CurrentHeight = CaveHeight + ExtraHeight;

            // Tünel zemini varyasyonu (ayarlara göre) - DÜZELTME: Daha sınırlı varyasyon
            int32 FloorOffset = 0;
            if (bNaturalCaveTunnels && CaveFloorVariation > 0)
            {
                float FloorNoise = FMath::PerlinNoise2D(FVector2D(TunnelX * 0.15f, TunnelY * 0.15f));
                // FloorOffset'i daha sınırlı tut ve pozitif değerlere odaklan
                FloorOffset = FMath::Clamp(FMath::RoundToInt(FloorNoise * CaveFloorVariation), -1, CaveFloorVariation);
            }

            // DÜZELTME: Cave floor'u daha güvenli hesapla
            int32 TunnelFloor = FMath::Max(CaveBaseHeight + FloorOffset, CaveBaseHeight - 1);
            int32 TunnelCeiling = TunnelFloor + CurrentHeight;

            // *** YENİ EKLENTİ: ÖNCE CAVE FLOOR'U OLUŞTUR ***
            // Cave zemini için grass/dirt blokları yerleştir (eğer yoksa)
            for (int32 FloorZ = 0; FloorZ <= TunnelFloor; FloorZ++)
            {
                FBlockPosition FloorBlockPos(LocalX, LocalY, FloorZ);
                EBlockType ExistingFloorType = GetBlockInternal(ChunkCoord, FloorBlockPos);

                // Eğer o konumda blok yoksa (Air ise), zemin bloğu yerleştir
                if (ExistingFloorType == EBlockType::Air)
                {
                    EBlockType FloorBlockType;
                    if (FloorZ == TunnelFloor)
                    {
                        // En üst zemin katmanı Grass
                        FloorBlockType = EBlockType::Grass;
                    }
                    else if (FloorZ >= TunnelFloor - 2)
                    {
                        // Üst katmanlar Dirt
                        FloorBlockType = EBlockType::Dirt;
                    }
                    else
                    {
                        // Alt katmanlar Stone
                        FloorBlockType = EBlockType::Stone;
                    }

                    SetBlockInternalWithoutReplication(ChunkCoord, FloorBlockPos, FloorBlockType);
                    UpdateBlockInstance(ChunkCoord, FloorBlockPos, FloorBlockType);
                    BlocksPlaced++;
                }
            }

            // Cave alanını temizle (sadece cave yüksekliği kadar)
            for (int32 Z = TunnelFloor + 1; Z < TunnelCeiling && Z < ChunkHeight; Z++)
            {
                FBlockPosition BlockPos(LocalX, LocalY, Z);
                EBlockType ExistingBlockType = GetBlockInternal(ChunkCoord, BlockPos);

                if (ExistingBlockType != EBlockType::Air)
                {
                    // *** UPDATED: Use chunk-based removal ***
                    RemoveBlockInstance(ChunkCoord, BlockPos, ExistingBlockType);
                    SetBlockInternalWithoutReplication(ChunkCoord, BlockPos, EBlockType::Air);

                    BlocksRemoved++;
                }
            }
        }
    }

    UE_LOG(LogTemp, Warning, TEXT("Natural tunnel: %d blocks removed, %d floor blocks placed"), BlocksRemoved, BlocksPlaced);
}

void ARandomMapGenerator::SealCaveEntrance(int32 CaveX, int32 CaveY, int32 DirX, int32 DirY, int32 CaveBaseHeight)
{
    UE_LOG(LogTemp, Warning, TEXT("Sealing cave entrance at (%d, %d)"), CaveX, CaveY);

    int32 InvisibleWallsPlaced = 0;

    // Sadece ilk katmanı kapat (Depth = 0)
    for (int32 WidthOffset = -CaveWidth / 2; WidthOffset <= CaveWidth / 2; WidthOffset++)
    {
        int32 SealX, SealY;

        if (DirX != 0) // Doğu/Batı mağaraları
        {
            SealX = CaveX;
            SealY = CaveY + WidthOffset;
        }
        else // Kuzey/Güney mağaraları
        {
            SealX = CaveX + WidthOffset;
            SealY = CaveY;
        }

        FChunkCoord SealChunkCoord(FMath::FloorToInt(static_cast<float>(SealX) / ChunkSize),
            FMath::FloorToInt(static_cast<float>(SealY) / ChunkSize));

        int32 SealLocalX = SealX % ChunkSize;
        if (SealLocalX < 0) SealLocalX += ChunkSize;

        int32 SealLocalY = SealY % ChunkSize;
        if (SealLocalY < 0) SealLocalY += ChunkSize;

        // Mağara girişinin alt kısmını kapat (üst kısmı açık bırak - daha doğal)
        int32 SealHeight = FMath::Max(1, CaveHeight - 2); // Alt 2/3'ünü kapat

        for (int32 Z = CaveBaseHeight; Z < CaveBaseHeight + SealHeight && Z < ChunkHeight; Z++)
        {
            FBlockPosition SealBlockPos(SealLocalX, SealLocalY, Z);
            PlaceInvisibleWall(SealChunkCoord, SealBlockPos);
            InvisibleWallsPlaced++;
        }
    }

    UE_LOG(LogTemp, Warning, TEXT("Cave entrance sealed: %d invisible walls placed"), InvisibleWallsPlaced);
}

// OLD CAVE SYSTEM - Backward compatibility için
void ARandomMapGenerator::GenerateSingleCave(int32 EdgeIndex)
{
    // Eski sistemi çağır - enhanced cave'in %50 pozisyonunda
    GenerateEnhancedCave(EdgeIndex, 0.5f);
}

int32 ARandomMapGenerator::GetMountainHeight(int32 WorldX, int32 WorldY, int32 DistanceFromEdge) const
{
    // Seed bazlı offset
    float XOffset = Seed * 0.007f;
    float YOffset = (Seed * 7919) * 0.007f;

    // Çoklu katmanlı Perlin noise
    float Noise1 = FMath::PerlinNoise2D(FVector2D(WorldX * MountainNoiseScale + XOffset,
        WorldY * MountainNoiseScale + YOffset));
    float Noise2 = FMath::PerlinNoise2D(FVector2D(WorldX * (MountainNoiseScale * 2.0f) + XOffset,
        WorldY * (MountainNoiseScale * 2.0f) + YOffset)) * 0.5f;
    float CombinedNoise = (Noise1 + Noise2) / 1.5f;

    // Kenara yaklaştıkça yükseklik artar
    float DistanceFactor = 1.0f - (static_cast<float>(DistanceFromEdge) / MountainBorderWidth);
    DistanceFactor = FMath::Pow(DistanceFactor, 0.6f); // Yumuşak geçiş

    // Yükseklik hesaplama
    int32 HeightRange = MountainMaxHeight - MountainMinHeight;
    int32 NoiseHeight = FMath::RoundToInt(HeightRange * (CombinedNoise + 1.0f) * 0.5f);
    int32 FinalHeight = MountainMinHeight + FMath::RoundToInt(NoiseHeight * DistanceFactor);

    return FMath::Clamp(FinalHeight, MountainMinHeight, MountainMaxHeight);
}

EBlockType ARandomMapGenerator::GetMountainBlockType(int32 HeightLayer, int32 TotalHeight, int32 LocalX, int32 LocalY) const
{
    float HeightPercent = static_cast<float>(HeightLayer) / TotalHeight;
    float PositionNoise = FMath::PerlinNoise2D(FVector2D(LocalX * 0.15f, LocalY * 0.15f));

    // Alt katmanlar (%0-30): Çoğunlukla Stone
    if (HeightPercent < 0.3f)
    {
        return EBlockType::Stone;
    }
    // Orta katmanlar (%30-70): Karışık (Stone + Dirt)
    else if (HeightPercent < 0.7f)
    {
        return (PositionNoise > 0.0f) ? EBlockType::Stone : EBlockType::Dirt;
    }
    // Üst katmanlar (%70-90): Çoğunlukla Dirt
    else if (HeightPercent < 0.9f)
    {
        return (PositionNoise > 0.3f) ? EBlockType::Dirt : EBlockType::Stone;
    }
    // En üst katmanlar (%90-100): Grass
    else
    {
        return (PositionNoise > -0.2f) ? EBlockType::Grass : EBlockType::Dirt;
    }
}

void ARandomMapGenerator::PlaceInvisibleWall(const FChunkCoord& ChunkCoord, const FBlockPosition& BlockPos)
{
    // Invisible wall bloğunu yerleştir
    SetBlockInternalWithoutReplication(ChunkCoord, BlockPos, EBlockType::InvisibleWall);

    // *** UPDATED: Direct chunk ISM addition ***
    UpdateBlockInstance(ChunkCoord, BlockPos, EBlockType::InvisibleWall);

    UE_LOG(LogTemp, VeryVerbose, TEXT("Invisible wall placed at chunk (%d,%d) local (%d,%d,%d)"),
        ChunkCoord.X, ChunkCoord.Y, BlockPos.X, BlockPos.Y, BlockPos.Z);
}

// *** CAVE SPAWN SYSTEM FUNCTIONS ***

void ARandomMapGenerator::RegisterCaveLocation(const FVector& EntranceLocation, const FVector& SpawnLocation, int32 EdgeIndex, float EdgePosition)
{
    FCaveLocation NewCaveLocation(EntranceLocation, SpawnLocation, EdgeIndex, EdgePosition);
    CaveLocations.Add(NewCaveLocation);

    UE_LOG(LogTemp, Warning, TEXT("Cave registered - Edge: %d, Position: %.2f, Entrance: %s, Spawn: %s"),
        EdgeIndex, EdgePosition, *EntranceLocation.ToString(), *SpawnLocation.ToString());
}

bool ARandomMapGenerator::GetCaveLocationByEdge(int32 EdgeIndex, FCaveLocation& OutCaveLocation) const
{
    for (const FCaveLocation& Cave : CaveLocations)
    {
        if (Cave.EdgeIndex == EdgeIndex)
        {
            OutCaveLocation = Cave;
            return true;
        }
    }

    // Mağara bulunamadı
    UE_LOG(LogTemp, Warning, TEXT("No cave found for edge %d"), EdgeIndex);
    return false;
}

void ARandomMapGenerator::DebugDrawCaveLocations() const
{
    if (!GetWorld())
    {
        UE_LOG(LogTemp, Warning, TEXT("Cannot draw debug - No valid world"));
        return;
    }

    if (CaveLocations.Num() == 0)
    {
        UE_LOG(LogTemp, Warning, TEXT("No cave locations to debug - CaveLocations is empty"));
        return;
    }

    UE_LOG(LogTemp, Warning, TEXT("=== DEBUGGING %d CAVE LOCATIONS ==="), CaveLocations.Num());

    for (int32 i = 0; i < CaveLocations.Num(); i++)
    {
        const FCaveLocation& Cave = CaveLocations[i];

        // Edge ismi
        FString EdgeName;
        FColor EdgeColor;
        switch (Cave.EdgeIndex)
        {
        case 0: EdgeName = TEXT("NORTH"); EdgeColor = FColor::Blue; break;
        case 1: EdgeName = TEXT("SOUTH"); EdgeColor = FColor::Red; break;
        case 2: EdgeName = TEXT("WEST"); EdgeColor = FColor::Green; break;
        case 3: EdgeName = TEXT("EAST"); EdgeColor = FColor::Yellow; break;
        default: EdgeName = TEXT("UNKNOWN"); EdgeColor = FColor::White; break;
        }

        // Mağara girişi - Edge renginde büyük küre
        DrawDebugSphere(GetWorld(), Cave.CaveEntranceLocation, 60.0f, 12, EdgeColor, true, 30.0f, 0, 3.0f);

        // Mağara spawn konumu - Kırmızı küre (enemy spawn yeri)
        DrawDebugSphere(GetWorld(), Cave.CaveSpawnLocation, 40.0f, 12, FColor::Red, true, 30.0f, 0, 2.0f);

        // Girişten spawn'a çizgi
        DrawDebugLine(GetWorld(), Cave.CaveEntranceLocation, Cave.CaveSpawnLocation, FColor::Purple, true, 30.0f, 0, 4.0f);

        // Bilgi metni
        FString InfoText = FString::Printf(TEXT("Cave %d: %s\nPos: %.1f\nSpawn Point"),
            i, *EdgeName, Cave.EdgePosition);
        DrawDebugString(GetWorld(), Cave.CaveSpawnLocation + FVector(0, 0, 80),
            InfoText, nullptr, FColor::White, 30.0f, true);

        // Log bilgisi
        UE_LOG(LogTemp, Warning, TEXT("Cave %d: %s Edge, Position %.2f, Entrance: %s, Spawn: %s"),
            i, *EdgeName, Cave.EdgePosition,
            *Cave.CaveEntranceLocation.ToString(), *Cave.CaveSpawnLocation.ToString());
    }

    UE_LOG(LogTemp, Warning, TEXT("=== CAVE DEBUG COMPLETE ==="));
}

void ARandomMapGenerator::TestCaveSpawnSystem(int32 TestSpawnerCount)
{
    UE_LOG(LogTemp, Warning, TEXT("=== TESTING CAVE SPAWN SYSTEM ==="));
    UE_LOG(LogTemp, Warning, TEXT("Requested spawners: %d"), TestSpawnerCount);
    UE_LOG(LogTemp, Warning, TEXT("Available caves: %d"), CaveLocations.Num());
    UE_LOG(LogTemp, Warning, TEXT("Cave spawn active: %s"), bSpawnEnemiesInCaves ? TEXT("YES") : TEXT("NO"));

    // Spawn lokasyonlarını al
    TArray<FVector> SpawnLocs = GetEnemySpawnLocations(TestSpawnerCount);

    UE_LOG(LogTemp, Warning, TEXT("Generated spawn locations: %d"), SpawnLocs.Num());

    // Her spawn lokasyonunu görselleştir
    for (int32 i = 0; i < SpawnLocs.Num(); i++)
    {
        if (GetWorld())
        {
            // Test spawn noktası - Turuncu küre
            DrawDebugSphere(GetWorld(), SpawnLocs[i], 25.0f, 8, FColor::Orange, true, 20.0f, 0, 2.0f);

            // Spawn numarası
            DrawDebugString(GetWorld(), SpawnLocs[i] + FVector(0, 0, 50),
                FString::Printf(TEXT("Spawn %d"), i), nullptr, FColor::Orange, 20.0f);
        }

        UE_LOG(LogTemp, Warning, TEXT("Spawn %d: %s"), i, *SpawnLocs[i].ToString());
    }

    // Mağara lokasyonlarını da göster
    DebugDrawCaveLocations();

    UE_LOG(LogTemp, Warning, TEXT("=== CAVE SPAWN TEST COMPLETE ==="));
}

// *** UPDATED: CHUNK-BASED GENERATION ***
void ARandomMapGenerator::GenerateChunk(const FChunkCoord& ChunkCoord)
{
    // *** UPDATED: Initialize chunk ISMs first ***
    InitializeChunkISMs(ChunkCoord);

    // Create chunk info and mark as generated
    FChunkInfo ChunkInfo(ChunkCoord);
    ChunkInfo.bIsGenerated = true;
    ChunksInfo.Add(ChunkCoord, ChunkInfo);

    // Her chunk için seed'e dayalı bir offset ekleyelim
    int32 ChunkSpecificSeedModifier = (ChunkCoord.X * 73 + ChunkCoord.Y * 31 + Seed) % 1000;

    // Generate terrain for this chunk
    for (int32 X = 0; X < ChunkSize; X++)
    {
        for (int32 Y = 0; Y < ChunkSize; Y++)
        {
            // Calculate world coordinates
            int32 WorldX = ChunkCoord.X * ChunkSize + X;
            int32 WorldY = ChunkCoord.Y * ChunkSize + Y;
            // Get height at this position (chunk specific modifier ile)
            int32 TerrainHeight = GetTerrainHeight(WorldX, WorldY, ChunkSpecificSeedModifier);

            // Generate blocks from bottom to top
            for (int32 Z = 0; Z < TerrainHeight; Z++)
            {
                EBlockType BlockType;
                // Blok tipleri - ÖNEMLİ: Üst katmanı her zaman Grass olarak ayarlıyoruz
                if (Z == TerrainHeight - 1)
                {
                    // En üst katman her zaman Grass
                    BlockType = EBlockType::Grass;
                }
                else if (Z < TerrainHeight - 4)
                {
                    // En derin katman Stone
                    BlockType = EBlockType::Stone;
                }
                else
                {
                    // Ortadaki katmanlar Dirt
                    BlockType = EBlockType::Dirt;
                }

                // Set block in chunk data
                FBlockPosition BlockPos(X, Y, Z);
                SetBlockInternalWithoutReplication(ChunkCoord, BlockPos, BlockType);

                // *** UPDATED: Direct chunk ISM addition instead of batch ***
                UpdateBlockInstance(ChunkCoord, BlockPos, BlockType);
            }

            // Chance to generate a tree on grass blocks
            float LocalTreeDensity = TreeDensity * 0.05f *
                (1.0f + FMath::Sin(WorldX * 0.02f + WorldY * 0.04f + Seed * 0.01f));
            LocalTreeDensity = FMath::Clamp(LocalTreeDensity, 0.0f, 0.1f);

            if (RandomStream.GetFraction() < LocalTreeDensity)
            {
                GenerateTree(WorldX, WorldY, TerrainHeight);
            }
        }
    }

    UE_LOG(LogTemp, Display, TEXT("Chunk (%d,%d) generated with chunk-based ISM"), ChunkCoord.X, ChunkCoord.Y);
}

void ARandomMapGenerator::GenerateTree(int32 WorldX, int32 WorldY, int32 WorldZ)
{
    // Convert world coordinates to chunk coordinates
    FChunkCoord ChunkCoord(FMath::FloorToInt(static_cast<float>(WorldX) / ChunkSize),
        FMath::FloorToInt(static_cast<float>(WorldY) / ChunkSize));
    // Convert world coordinates to local chunk coordinates
    int32 LocalX = WorldX % ChunkSize;
    int32 LocalY = WorldY % ChunkSize;

    // Seed'e dayalı çeşitli ağaç yükseklikleri
    int32 BaseTreeHeight = RandomStream.RandRange(3, 6);
    int32 TreeHeightOffset = ((WorldX * 31) + (WorldY * 17) + Seed) % 3 - 1; // -1, 0, or 1
    int32 TreeHeight = FMath::Clamp(BaseTreeHeight + TreeHeightOffset, 3, 8);

    EBlockType TrunkType = EBlockType::Wood;

    // Generate trunk
    for (int32 Z = 0; Z < TreeHeight; Z++)
    {
        FBlockPosition BlockPos(LocalX, LocalY, WorldZ + Z);
        // Make sure we're in chunk bounds
        if (BlockPos.Z < ChunkHeight)
        {
            // Set block data
            SetBlockInternalWithoutReplication(ChunkCoord, BlockPos, TrunkType);

            // *** UPDATED: Direct chunk ISM addition ***
            UpdateBlockInstance(ChunkCoord, BlockPos, TrunkType);
        }
    }

    // Seed'e dayalı yaprak boyutu
    int32 LeafSize = RandomStream.RandRange(2, 3);
    EBlockType LeafType = EBlockType::Leaves;

    // Generate leaves (vary size based on seed)
    for (int32 LX = -LeafSize; LX <= LeafSize; LX++)
    {
        for (int32 LY = -LeafSize; LY <= LeafSize; LY++)
        {
            int32 LeafHeight = RandomStream.RandRange(2, 3);
            for (int32 LZ = 0; LZ <= LeafHeight; LZ++)
            {
                // Skip trunk positions
                if (LX == 0 && LY == 0 && LZ < LeafHeight)
                    continue;

                // Kenarlar için daha az yaprak olasılığı (daha doğal ağaç şekli)
                if ((FMath::Abs(LX) == LeafSize || FMath::Abs(LY) == LeafSize) &&
                    RandomStream.GetFraction() > 0.4f)
                    continue;

                int32 LeafX = LocalX + LX;
                int32 LeafY = LocalY + LY;
                int32 LeafZ = WorldZ + TreeHeight - 1 + LZ;

                // Make sure leaves are within chunk bounds
                if (LeafX >= 0 && LeafX < ChunkSize &&
                    LeafY >= 0 && LeafY < ChunkSize &&
                    LeafZ >= 0 && LeafZ < ChunkHeight)
                {
                    FBlockPosition BlockPos(LeafX, LeafY, LeafZ);
                    // Set block data
                    SetBlockInternalWithoutReplication(ChunkCoord, BlockPos, LeafType);

                    // *** UPDATED: Direct chunk ISM addition ***
                    UpdateBlockInstance(ChunkCoord, BlockPos, LeafType);
                }
            }
        }
    }
}

int32 ARandomMapGenerator::GetTerrainHeight(int32 WorldX, int32 WorldY, int32 ChunkSeedModifier) const
{
    // Haritanın merkezini hesapla
    int32 CenterWorldX = (WorldSizeInChunks * ChunkSize) / 2;
    int32 CenterWorldY = (WorldSizeInChunks * ChunkSize) / 2;

    // Merkeze olan mesafeyi hesapla
    int32 DistanceX = abs(WorldX - CenterWorldX);
    int32 DistanceY = abs(WorldY - CenterWorldY);
    float Distance = FMath::Sqrt(static_cast<float>(DistanceX * DistanceX + DistanceY * DistanceY));

    // Base Core merkez bölgesi için düz alan yarat
    float FlatnessFactor = 1.0f;

    // Merkeze yakınsa düzleştir
    if (Distance < BaseCoreCenter * ChunkSize)
    {
        // Merkezde tamamen düz, kenarlara doğru kademeli geçiş
        FlatnessFactor = FMath::Clamp(Distance / (BaseCoreCenter * ChunkSize), 0.1f, 1.0f);
    }

    // Apply Perlin noise to base height
    float Noise = GetPerlinNoise(WorldX, WorldY);

    // Chunk spesifik seed modifierini bir miktar ekleyelim
    float ChunkModifier = static_cast<float>(ChunkSeedModifier) / 2000.0f; // ±0.5 arasında

    // Map noise (-1 to 1) to height variation
    float HeightOffset = Noise * HeightVariation;

    // Merkeze yakınsa düzleştir (FlatnessFactor ile çarp)
    HeightOffset *= FlatnessFactor;

    // Chunk bazlı ek varyasyon
    HeightOffset += ChunkModifier * HeightVariation * FlatnessFactor;

    // Küçük bir ikincil gürültü ekleyelim (daha fazla detay için)
    float SecondaryNoise = FMath::PerlinNoise2D(FVector2D(WorldX * 0.1f + Seed * 0.01f, WorldY * 0.1f)) * 2.0f;
    HeightOffset += SecondaryNoise * FlatnessFactor;

    // Adjust height based on flatness
    float MapFlatnessFactor = 1.0f - MapFlatness;
    HeightOffset *= MapFlatnessFactor;

    // Calculate final height with a seed-based modifier
    float SeedHeightModifier = (Seed % 50) / 10.0f - 2.5f; // -2.5 ile +2.5 arası
    int32 Height = BaseHeight + FMath::RoundToInt(HeightOffset) + FMath::RoundToInt(SeedHeightModifier * FlatnessFactor);

    // Clamp height to valid range
    return FMath::Clamp(Height, 1, ChunkHeight - 1);
}

float ARandomMapGenerator::GetPerlinNoise(float X, float Y) const
{
    // Daha belirgin farklılıklar oluşturmak için seed'e dayalı offset ekleyelim
    float XOffset = (Seed % 10000) * 0.01f;
    float YOffset = (Seed % 7919) * 0.01f; // Asal sayı kullanıyoruz daha iyi dağılım için
    // Girdi koordinatlarını doğrudan seed'den etkilenen bir faktörle genişletelim
    float SeedFactor = 1.0f + ((Seed % 1000) / 10000.0f);
    // Scaled coordinates with offsets and seed factor
    float ScaledX = (X * NoiseScale * SeedFactor) + XOffset;
    float ScaledY = (Y * NoiseScale * SeedFactor) + YOffset;
    // İlk Perlin gürültüsü
    float Noise1 = FMath::PerlinNoise2D(FVector2D(ScaledX, ScaledY));
    // Seed'den etkilenen farklı bir ölçekte ikinci bir Perlin gürültüsü ekleyelim
    // Bu, farklı seed'ler için daha çeşitli haritalar oluşturacak
    float ScaleModifier = 0.5f + (float)(Seed % 5000) / 10000.0f;
    float Noise2 = FMath::PerlinNoise2D(FVector2D(ScaledX * 2.0f * ScaleModifier, ScaledY * 2.0f * ScaleModifier)) * 0.5f;
    // Seed'e dayalı üçüncü bir gürültü
    float Noise3 = FMath::PerlinNoise2D(FVector2D(ScaledX * 4.0f * (1.0f - ScaleModifier), ScaledY * 4.0f * (1.0f - ScaleModifier))) * 0.25f;
    // Tüm gürültüleri birleştir
    float FinalNoise = (Noise1 + Noise2 + Noise3) / 1.75f;
    // Sınırlama [-1, 1]
    return FMath::Clamp(FinalNoise, -1.0f, 1.0f);
}

void ARandomMapGenerator::SetBlockTypeAtPosition(const FVector& WorldLocation, EBlockType BlockType)
{
    // Only server can modify blocks
    if (!HasAuthority())
        return;
    // Convert world position to chunk and block coordinates
    FChunkCoord ChunkCoord = WorldToChunkCoord(WorldLocation);
    FBlockPosition BlockPos = WorldToBlockPosition(WorldLocation);
    // Ensure block position is valid
    if (BlockPos.X < 0 || BlockPos.X >= ChunkSize ||
        BlockPos.Y < 0 || BlockPos.Y >= ChunkSize ||
        BlockPos.Z < 0 || BlockPos.Z >= ChunkHeight)
    {
        return;
    }
    // Make sure chunk is tracked
    if (!ChunksInfo.Contains(ChunkCoord))
    {
        // Create new chunk info if it doesn't exist
        FChunkInfo ChunkInfo(ChunkCoord);
        ChunkInfo.bIsGenerated = true;
        ChunksInfo.Add(ChunkCoord, ChunkInfo);
    }
    // Get old block type
    EBlockType OldBlockType = GetBlockInternal(ChunkCoord, BlockPos);
    // If the block type hasn't changed, do nothing
    if (OldBlockType == BlockType)
        return;
    // Log ekleme
    UE_LOG(LogTemp, Display, TEXT("SetBlockTypeAtPosition: %s pozisyonundaki %d blok tipi %d olarak değiştiriliyor"),
        *WorldLocation.ToString(), static_cast<int32>(OldBlockType), static_cast<int32>(BlockType));
    // Update block data on server
    SetBlockInternalWithoutReplication(ChunkCoord, BlockPos, BlockType);
    // Clear damage data if block is removed or changed
    FWorldBlockKey Key(ChunkCoord, BlockPos);
    if (BlockType == EBlockType::Air || OldBlockType != BlockType)
    {
        BlockDamageData.Remove(Key);
        // Also clear from DestroyedBlocksProcessed map
        DestroyedBlocksProcessed.Remove(Key);
        // Temizle from ProcessedDestroyedBlocks
        FString BlockKey = FString::Printf(TEXT("%d_%d_%d_%d_%d"),
            ChunkCoord.X, ChunkCoord.Y,
            BlockPos.X, BlockPos.Y, BlockPos.Z);
        ProcessedDestroyedBlocks.Remove(BlockKey);
    }
    // Eski bloğu temizle - özellikle önemli!
    if (OldBlockType != EBlockType::Air)
    {
        RemoveBlockInstance(ChunkCoord, BlockPos, OldBlockType);
    }
    // Yeni bloğu ekle
    if (BlockType != EBlockType::Air)
    {
        UpdateBlockInstance(ChunkCoord, BlockPos, BlockType);
    }
    // Replicate to clients
    MulticastUpdateBlock(ChunkCoord, BlockPos, BlockType);
}

EBlockType ARandomMapGenerator::GetBlockTypeAtPosition(const FVector& WorldLocation) const
{
    // Convert world position to chunk and block coordinates
    FChunkCoord ChunkCoord = WorldToChunkCoord(WorldLocation);
    FBlockPosition BlockPos = WorldToBlockPosition(WorldLocation);
    // Ensure block position is valid
    if (BlockPos.X < 0 || BlockPos.X >= ChunkSize ||
        BlockPos.Y < 0 || BlockPos.Y >= ChunkSize ||
        BlockPos.Z < 0 || BlockPos.Z >= ChunkHeight)
    {
        return EBlockType::Air;
    }
    return GetBlockInternal(ChunkCoord, BlockPos);
}

// *** UPDATED: CHUNK-BASED INSTANCE MANAGEMENT ***
void ARandomMapGenerator::UpdateBlockInstance(const FChunkCoord& ChunkCoord, const FBlockPosition& BlockPos, EBlockType BlockType)
{
    if (BlockType == EBlockType::Air) return;

    // Chunk ISM'leri yoksa oluştur
    if (!ChunkISMSystem.Contains(ChunkCoord))
    {
        InitializeChunkISMs(ChunkCoord);
    }

    FChunkISMData& ChunkData = ChunkISMSystem[ChunkCoord];
    UInstancedStaticMeshComponent* ChunkISM = ChunkData.ChunkISMs.FindRef(BlockType);

    if (!ChunkISM)
    {
        UE_LOG(LogTemp, Error, TEXT("No chunk ISM found for block type %d in chunk (%d,%d)"),
            (int32)BlockType, ChunkCoord.X, ChunkCoord.Y);
        return;
    }

    // World position hesapla
    FVector WorldPosition = BlockToWorldPosition(ChunkCoord, BlockPos);
    FTransform InstanceTransform = FTransform(FRotator::ZeroRotator, WorldPosition);

    // Instance'ı ekle
    int32 InstanceIndex = ChunkISM->AddInstance(InstanceTransform);

    // Instance mapping'e ekle - FIXED: Combined key approach
    FBlockTypePositionKey MappingKey(BlockType, BlockPos);
    ChunkData.InstanceIndexMapping.Add(MappingKey, InstanceIndex);
    ChunkData.InstanceCounts[BlockType]++;

    UE_LOG(LogTemp, VeryVerbose, TEXT("Added instance %d for block type %d at chunk (%d,%d) local pos (%d,%d,%d) world pos %s"),
        InstanceIndex, (int32)BlockType, ChunkCoord.X, ChunkCoord.Y,
        BlockPos.X, BlockPos.Y, BlockPos.Z, *WorldPosition.ToString());
}

void ARandomMapGenerator::RemoveBlockInstance(const FChunkCoord& ChunkCoord, const FBlockPosition& BlockPos, EBlockType BlockType)
{
    if (BlockType == EBlockType::Air) return;

    if (!ChunkISMSystem.Contains(ChunkCoord))
    {
        UE_LOG(LogTemp, Warning, TEXT("No chunk ISM data for chunk (%d,%d) when trying to remove block"),
            ChunkCoord.X, ChunkCoord.Y);
        return;
    }

    FChunkISMData& ChunkData = ChunkISMSystem[ChunkCoord];
    UInstancedStaticMeshComponent* ChunkISM = ChunkData.ChunkISMs.FindRef(BlockType);

    if (!ChunkISM)
    {
        UE_LOG(LogTemp, Warning, TEXT("No chunk ISM for block type %d in chunk (%d,%d)"),
            (int32)BlockType, ChunkCoord.X, ChunkCoord.Y);
        return;
    }

    // Instance mapping'den instance index'i bul - FIXED: Combined key approach
    FBlockTypePositionKey MappingKey(BlockType, BlockPos);
    int32* FoundInstanceIndex = ChunkData.InstanceIndexMapping.Find(MappingKey);

    if (!FoundInstanceIndex)
    {
        UE_LOG(LogTemp, Warning, TEXT("Instance not found for block pos (%d,%d,%d) type %d in chunk (%d,%d)"),
            BlockPos.X, BlockPos.Y, BlockPos.Z, (int32)BlockType, ChunkCoord.X, ChunkCoord.Y);

        // Debug: Mapping'deki tüm instance'ları listele
        UE_LOG(LogTemp, Warning, TEXT("Available instances in chunk (%d,%d):"),
            ChunkCoord.X, ChunkCoord.Y);
        for (const auto& Pair : ChunkData.InstanceIndexMapping)
        {
            if (Pair.Key.BlockType == BlockType)
            {
                UE_LOG(LogTemp, Warning, TEXT("  Type %d Pos(%d,%d,%d) -> Instance %d"),
                    (int32)Pair.Key.BlockType, Pair.Key.BlockPos.X, Pair.Key.BlockPos.Y, Pair.Key.BlockPos.Z, Pair.Value);
            }
        }
        return;
    }

    int32 InstanceIndexToRemove = *FoundInstanceIndex;

    // Double-check: Instance'ın pozisyonu doğru mu?
    FTransform InstanceTransform;
    if (ChunkISM->GetInstanceTransform(InstanceIndexToRemove, InstanceTransform))
    {
        FVector ExpectedWorldPos = BlockToWorldPosition(ChunkCoord, BlockPos);
        float DistanceSq = FVector::DistSquared(InstanceTransform.GetLocation(), ExpectedWorldPos);

        if (DistanceSq > 1.0f) // 1 unit tolerance
        {
            UE_LOG(LogTemp, Error, TEXT("Instance position mismatch! Expected: %s, Found: %s, Distance: %f"),
                *ExpectedWorldPos.ToString(), *InstanceTransform.GetLocation().ToString(), FMath::Sqrt(DistanceSq));
        }
    }

    // Yeni:
    if (SwapRemoveInstance(ChunkISM, ChunkData, MappingKey))
    {
        ChunkData.InstanceCounts[BlockType]--;
    };

    UE_LOG(LogTemp, Display, TEXT("Successfully removed instance %d for block type %d at chunk (%d,%d) pos (%d,%d,%d)"),
        InstanceIndexToRemove, (int32)BlockType, ChunkCoord.X, ChunkCoord.Y, BlockPos.X, BlockPos.Y, BlockPos.Z);
}

// *** NEW: UPDATE CHUNK INSTANCE INDICES ***
void ARandomMapGenerator::UpdateChunkInstanceIndicesAfterRemoval(const FChunkCoord& ChunkCoord, EBlockType BlockType, int32 RemovedIndex)
{
    if (!ChunkISMSystem.Contains(ChunkCoord)) return;

    FChunkISMData& ChunkData = ChunkISMSystem[ChunkCoord];

    // Kaldırılan index'ten büyük olan tüm index'leri 1 azalt - FIXED: Combined key approach
    int32 UpdatedCount = 0;
    TArray<FBlockTypePositionKey> KeysToUpdate;
    TArray<int32> NewIndices;

    // Önce güncellenecek key'leri ve yeni index'leri topla
    for (auto& Pair : ChunkData.InstanceIndexMapping)
    {
        if (Pair.Key.BlockType == BlockType && Pair.Value > RemovedIndex)
        {
            KeysToUpdate.Add(Pair.Key);
            NewIndices.Add(Pair.Value - 1);
        }
    }

    // Sonra güncelle (iterator invalidation'ı önlemek için)
    for (int32 i = 0; i < KeysToUpdate.Num(); i++)
    {
        ChunkData.InstanceIndexMapping[KeysToUpdate[i]] = NewIndices[i];
        UpdatedCount++;
    }

    UE_LOG(LogTemp, VeryVerbose, TEXT("Updated %d instance indices after removing index %d in chunk (%d,%d) type %d"),
        UpdatedCount, RemovedIndex, ChunkCoord.X, ChunkCoord.Y, (int32)BlockType);
}

bool ARandomMapGenerator::FindNearestBlock(const FVector& StartLocation, EBlockType BlockType, float MaxDistance, FVector& OutBlockLocation)
{
    // Hata ayıklama için log ekle - UPDATED
    LogDebugMessage(EDebugCategory::WorldGeneration,
        FString::Printf(TEXT("FindNearestBlock çağrıldı: Konum=%s, Tip=%d, MaksimumMesafe=%.2f"),
            *StartLocation.ToString(), static_cast<int32>(BlockType), MaxDistance));

    // Convert world position to chunk coordinates
    FChunkCoord CenterChunkCoord = WorldToChunkCoord(StartLocation);

    // Calculate number of chunks to search (based on max distance)
    int32 ChunkSearchRadius = FMath::CeilToInt(MaxDistance / (ChunkSize * BlockSize)) + 1;

    float MinDistanceSquared = MaxDistance * MaxDistance;
    bool bFoundBlock = false;

    // Debug için sayaçlar ekle
    int32 ChunksChecked = 0;
    int32 BlocksChecked = 0;
    int32 MatchingBlocksFound = 0;

    // Search chunks within radius
    for (int32 ChunkX = CenterChunkCoord.X - ChunkSearchRadius; ChunkX <= CenterChunkCoord.X + ChunkSearchRadius; ChunkX++)
    {
        for (int32 ChunkY = CenterChunkCoord.Y - ChunkSearchRadius; ChunkY <= CenterChunkCoord.Y + ChunkSearchRadius; ChunkY++)
        {
            FChunkCoord SearchChunkCoord(ChunkX, ChunkY);

            // Get chunk info
            const FChunkInfo* ChunkInfo = ChunksInfo.Find(SearchChunkCoord);
            if (!ChunkInfo || !ChunkInfo->bIsGenerated)
                continue;

            ChunksChecked++;

            // Search all blocks in chunk
            for (int32 X = 0; X < ChunkSize; X++)
            {
                for (int32 Y = 0; Y < ChunkSize; Y++)
                {
                    for (int32 Z = 0; Z < ChunkHeight; Z++)
                    {
                        FBlockPosition BlockPos(X, Y, Z);
                        EBlockType CurrBlockType = GetBlockInternal(SearchChunkCoord, BlockPos);
                        BlocksChecked++;

                        // ÖNEMLİ DEĞİŞİKLİK: Kontrol ifadesi güncellendi
                        // BlockType ALL ise herhangi bir blok tipini kabul et (Air hariç)
                        // VEYA spesifik blok tipi eşleşiyorsa (Air hariç)
                        if ((BlockType == EBlockType::ALL || CurrBlockType == BlockType) && CurrBlockType != EBlockType::Air)
                        {
                            MatchingBlocksFound++;

                            // Get world position of block
                            FVector BlockPosition = BlockToWorldPosition(SearchChunkCoord, BlockPos);

                            // Calculate squared distance
                            float DistanceSquared = FVector::DistSquared(StartLocation, BlockPosition);

                            // Update if closer than current best
                            if (DistanceSquared < MinDistanceSquared)
                            {
                                MinDistanceSquared = DistanceSquared;
                                OutBlockLocation = BlockPosition;
                                bFoundBlock = true;
                            }
                        }
                    }
                }
            }
        }
    }

    // Hata ayıklama log bilgisi - UPDATED
    if (bFoundBlock)
    {
        LogDebugMessage(EDebugCategory::WorldGeneration,
            FString::Printf(TEXT("FindNearestBlock: Blok bulundu! Konum=%s, Mesafe=%.2f, %d chunk kontrol edildi, %d blok incelendi, %d eşleşme bulundu"),
                *OutBlockLocation.ToString(), FMath::Sqrt(MinDistanceSquared), ChunksChecked, BlocksChecked, MatchingBlocksFound));

        // Debug görselleştirme - UPDATED
        DrawDebugSphereIfEnabled(EDebugCategory::WorldGeneration, OutBlockLocation, 20.0f, FColor::Green);
        DrawDebugLineIfEnabled(EDebugCategory::WorldGeneration, StartLocation, OutBlockLocation, FColor::Blue);
    }
    else
    {
        LogDebugMessage(EDebugCategory::WorldGeneration,
            FString::Printf(TEXT("FindNearestBlock: Blok bulunamadı! %d chunk kontrol edildi, %d blok incelendi"),
                ChunksChecked, BlocksChecked), true);

        // Arama alanını göster - UPDATED
        DrawDebugSphereIfEnabled(EDebugCategory::WorldGeneration, StartLocation, MaxDistance, FColor::Red);
    }

    return bFoundBlock;
}

FChunkCoord ARandomMapGenerator::WorldToChunkCoord(const FVector& WorldLocation) const
{
    // Calculate block size with spacing
    float EffectiveBlockSize = BlockSize + BlockSpacing;
    // Kayan nokta hassasiyet sorunlarını çözmek için epsilon ekle
    float Epsilon = 0.001f;
    // Convert world location to chunk coordinates
    int32 ChunkX = FMath::FloorToInt((WorldLocation.X + Epsilon) / (ChunkSize * EffectiveBlockSize));
    int32 ChunkY = FMath::FloorToInt((WorldLocation.Y + Epsilon) / (ChunkSize * EffectiveBlockSize));
    return FChunkCoord(ChunkX, ChunkY);
}

FBlockPosition ARandomMapGenerator::WorldToBlockPosition(const FVector& WorldLocation) const
{
    // Calculate block size with spacing
    float EffectiveBlockSize = BlockSize + BlockSpacing;
    // Kayan nokta hassasiyet sorunlarını çözmek için epsilon ekle
    float Epsilon = 0.001f;
    // Convert world location to absolute block coordinates
    int32 AbsBlockX = FMath::FloorToInt((WorldLocation.X + Epsilon) / EffectiveBlockSize);
    int32 AbsBlockY = FMath::FloorToInt((WorldLocation.Y + Epsilon) / EffectiveBlockSize);
    int32 BlockZ = FMath::FloorToInt((WorldLocation.Z + Epsilon) / EffectiveBlockSize);
    // Calculate chunk coordinates
    int32 ChunkX = FMath::FloorToInt(static_cast<float>(AbsBlockX) / ChunkSize);
    int32 ChunkY = FMath::FloorToInt(static_cast<float>(AbsBlockY) / ChunkSize);
    // Calculate local block coordinates within chunk
    int32 LocalBlockX = AbsBlockX - (ChunkX * ChunkSize);
    int32 LocalBlockY = AbsBlockY - (ChunkY * ChunkSize);
    return FBlockPosition(LocalBlockX, LocalBlockY, BlockZ);
}

FVector ARandomMapGenerator::BlockToWorldPosition(const FChunkCoord& ChunkCoord, const FBlockPosition& BlockPos) const
{
    // Calculate block size with spacing
    float EffectiveBlockSize = BlockSize + BlockSpacing;
    // Calculate absolute block coordinates
    int32 AbsBlockX = ChunkCoord.X * ChunkSize + BlockPos.X;
    int32 AbsBlockY = ChunkCoord.Y * ChunkSize + BlockPos.Y;
    // Convert to world coordinates
    float WorldX = AbsBlockX * EffectiveBlockSize + (BlockSize / 2.0f);
    float WorldY = AbsBlockY * EffectiveBlockSize + (BlockSize / 2.0f);
    float WorldZ = BlockPos.Z * EffectiveBlockSize + (BlockSize / 2.0f);
    return FVector(WorldX, WorldY, WorldZ);
}

bool ARandomMapGenerator::IsChunkGenerated(const FChunkCoord& ChunkCoord) const
{
    const FChunkInfo* ChunkInfo = ChunksInfo.Find(ChunkCoord);
    return ChunkInfo && ChunkInfo->bIsGenerated;
}

void ARandomMapGenerator::MulticastUpdateBlock_Implementation(const FChunkCoord& ChunkCoord, const FBlockPosition& BlockPos, EBlockType BlockType)
{
    // Veri güncelleme
    EBlockType OldBlockType = GetBlockInternal(ChunkCoord, BlockPos);
    // Debug için blok konumu ve diğer bilgileri logla
    FVector WorldPos = BlockToWorldPosition(ChunkCoord, BlockPos);
    UE_LOG(LogTemp, Display, TEXT("%s: MulticastUpdateBlock - Position: %s (%d,%d,%d), OldType: %d, NewType: %d"),
        HasAuthority() ? TEXT("SERVER") : TEXT("CLIENT"),
        *WorldPos.ToString(), BlockPos.X, BlockPos.Y, BlockPos.Z,
        static_cast<int32>(OldBlockType), static_cast<int32>(BlockType));
    // Veri güncelleme
    SetBlockInternalWithoutReplication(ChunkCoord, BlockPos, BlockType);
    // Client tarafında görsel güncelleme yapmalıyız
    // NOT: Server tarafında ApplyDamageToBlock içerisinde görsel güncelleme ZATEN yapılmıştır
    if (!HasAuthority())
    {
        // Eski bloğun görsel instance'ını kaldır (Air değilse ve değiştiyse)
        if (OldBlockType != EBlockType::Air && OldBlockType != BlockType)  // <- IF KOŞULU EKLENDİ
        {
            UE_LOG(LogTemp, Warning, TEXT("CLIENT: Removing block instance at (%d,%d,%d) type: %d"),
                BlockPos.X, BlockPos.Y, BlockPos.Z, static_cast<int32>(OldBlockType));
            // Debug görselleştirme ekleyelim
            DrawDebugBox(GetWorld(), WorldPos, FVector(BlockSize / 2.0f),
                FQuat::Identity, FColor::Red, false, 3.0f);
            RemoveBlockInstance(ChunkCoord, BlockPos, OldBlockType);
        }
        // Yeni bloğu ekle (Air değilse)
        if (BlockType != EBlockType::Air)
        {
            UpdateBlockInstance(ChunkCoord, BlockPos, BlockType);
            // Debug görselleştirme ekleyelim
            DrawDebugBox(GetWorld(), WorldPos, FVector(BlockSize / 2.0f),
                FQuat::Identity, FColor::Green, false, 3.0f);
        }
    }
    // Server ise basit bir debug görseli ekle ama işlem yapma (ApplyDamageToBlock zaten yaptı)
    else
    {
        // Sadece görselleştirme amaçlı
        if (BlockType != OldBlockType)
        {
            DrawDebugBox(GetWorld(), WorldPos, FVector(BlockSize / 3.0f),
                FQuat::Identity, FColor::Blue, false, 1.0f);
        }
    }
}

void ARandomMapGenerator::SetBlockInternalWithoutReplication(const FChunkCoord& ChunkCoord, const FBlockPosition& BlockPos, EBlockType BlockType)
{
    FWorldBlockKey Key(ChunkCoord, BlockPos);
    if (BlockType == EBlockType::Air)
    {
        // Remove block if it's air
        BlocksData.Remove(Key);
    }
    else
    {
        // Set or override block
        BlocksData.Add(Key, BlockType);
    }
}

EBlockType ARandomMapGenerator::GetBlockInternal(const FChunkCoord& ChunkCoord, const FBlockPosition& BlockPos) const
{
    FWorldBlockKey Key(ChunkCoord, BlockPos);
    const EBlockType* BlockType = BlocksData.Find(Key);
    return BlockType ? *BlockType : EBlockType::Air;
}

// *** UPDATED DAMAGE SYSTEM WITH CHUNK-BASED ISM ***
bool ARandomMapGenerator::ApplyDamageToBlock(const FVector& WorldLocation, float Damage, AActor* DamageInstigator, AActor* DamageCauser, TSubclassOf<UDamageType> DamageType)
{
    // Sadece sunucu hasarı uygulayabilir
    if (!HasAuthority())
        return false;

    // Sınır bloğu kontrolü (geçerli harita sınırlarının dışında mı)
    FChunkCoord ChunkCoord = WorldToChunkCoord(WorldLocation);
    int32 ChunkX = ChunkCoord.X;
    int32 ChunkY = ChunkCoord.Y;

    // Chunk geçerli harita aralığının dışındaysa, bir sınır bloğudur
    if (ChunkX < 0 || ChunkY < 0 || ChunkX >= WorldSizeInChunks || ChunkY >= WorldSizeInChunks)
    {
        // Sınır blokları yıkılamaz (Mountain border blokları)
        LogDebugMessage(EDebugCategory::BlockPlacement,
            FString::Printf(TEXT("%s konumundaki blok bir mountain border bloğudur ve yıkılamaz."), *WorldLocation.ToString()));
        return false;
    }

    // Dünya konumundan blok koordinatlarına dönüştür
    FBlockPosition BlockPos = WorldToBlockPosition(WorldLocation);
    // Blok pozisyonunun geçerli olduğundan emin ol
    if (BlockPos.X < 0 || BlockPos.X >= ChunkSize ||
        BlockPos.Y < 0 || BlockPos.Y >= ChunkSize ||
        BlockPos.Z < 0 || BlockPos.Z >= ChunkHeight)
    {
        return false;
    }

    // Bu konumdaki blok tipini al
    FWorldBlockKey Key(ChunkCoord, BlockPos);
    EBlockType BlockType = GetBlockInternal(ChunkCoord, BlockPos);

    // Hava bloğuna hasar uygulamaya gerek yok
    if (BlockType == EBlockType::Air)
    {
        LogDebugMessage(EDebugCategory::BlockPlacement,
            FString::Printf(TEXT("SERVER: ApplyDamageToBlock - Konum %s'de blok yok (Air)"),
                *WorldLocation.ToString()), true);
        return false;
    }

    // *** YENİ KONTROL: INVISIBLE WALL BLOKLARI YIKILAMAMENLİ ***
    if (BlockType == EBlockType::InvisibleWall)
    {
        LogDebugMessage(EDebugCategory::BlockPlacement,
            FString::Printf(TEXT("SERVER: Invisible Wall bloğu yıkılamaz! Konum: %s"), *WorldLocation.ToString()));
        return false;
    }

    // Debug bilgisi ekle - UPDATED
    LogDebugMessage(EDebugCategory::BlockPlacement,
        FString::Printf(TEXT("SERVER: ApplyDamageToBlock - Konum %s (%d,%d,%d), Blok Tipi: %d, Hasar: %f"),
            *WorldLocation.ToString(), BlockPos.X, BlockPos.Y, BlockPos.Z, static_cast<int32>(BlockType), Damage));

    // Blok dayanıklılık verisini al veya oluştur
    FBlockDamageData* DamageData = BlockDamageData.Find(Key);
    if (!DamageData)
    {
        // Bu blok için maks can değerini data table'dan al
        float MaxHealth = 100.0f; // Varsayılan değer
        if (BlockDataTable)
        {
            FString BlockTypeStr = UEnum::GetValueAsString(BlockType).Replace(TEXT("EBlockType::"), TEXT(""));
            FBlockData* BlockDataRow = BlockDataTable->FindRow<FBlockData>(FName(*BlockTypeStr), TEXT(""));
            if (BlockDataRow)
            {
                MaxHealth = BlockDataRow->Durability;
            }
        }

        // Yeni hasar verisi oluştur
        BlockDamageData.Add(Key, FBlockDamageData(MaxHealth));
        DamageData = BlockDamageData.Find(Key);
    }

    // Hasar uygula
    DamageData->CurrentHealth -= Damage;
    DamageData->LastDamageInstigator = DamageInstigator;
    DamageData->LastDamageCauser = DamageCauser;
    DamageData->LastDamageType = DamageType;

    // Hasar delegatesi çağır
    FVector BlockWorldLocation = BlockToWorldPosition(ChunkCoord, BlockPos);
    FName ItemName = NAME_None;
    if (BlockDataTable)
    {
        FString BlockTypeStr = UEnum::GetValueAsString(BlockType).Replace(TEXT("EBlockType::"), TEXT(""));
        FBlockData* BlockDataRow = BlockDataTable->FindRow<FBlockData>(FName(*BlockTypeStr), TEXT(""));
        if (BlockDataRow)
        {
            ItemName = BlockDataRow->ItemName;
        }
    }

    // Hasar delegatesi çağır (ItemName ekli)
    OnBlockDamaged.Broadcast(BlockWorldLocation, BlockType, ItemName, Damage, DamageInstigator, DamageCauser, DamageType);

    // Debug için hasar durumunu görselleştir - UPDATED
    DrawDebugSphereIfEnabled(EDebugCategory::BlockPlacement, BlockWorldLocation, 10.0f, FColor::Yellow);

    FString HealthText = FString::Printf(TEXT("%.1f / %.1f"), DamageData->CurrentHealth, DamageData->MaxHealth);
    DrawDebugString(GetWorld(), BlockWorldLocation + FVector(0, 0, 20), *HealthText, nullptr, FColor::White, 1.0f);

    // İstemcilere hasar güncellemesini bildir - istemcilerdeki debug görselleştirmesi buradan gelecek
    MulticastBlockDamaged(ChunkCoord, BlockPos, DamageData->CurrentHealth, DamageInstigator, DamageCauser, DamageType);

    // Blok yok edildi mi değişkeni
    bool bIsBlockDestroyed = false;

    // Blok sağlığı sıfır veya daha az ise, bloğu kır
    if (DamageData->CurrentHealth <= 0.0f)
    {
        // Blok yok edildi olarak işaretle
        bIsBlockDestroyed = true;

        // ÖNEMLİ: Bu kısım sadece SERVER tarafında çalışır (HasAuthority() kontrolü zaten fonksiyonun başında var)
        // Blok yok edildi delegatesi çağır - SADECE SERVER'DA
        FName BlockItemName = GetItemNameForBlockType(BlockType);

        // Log ekle - UPDATED
        LogDebugMessage(EDebugCategory::BlockPlacement,
            FString::Printf(TEXT("SERVER - CALLING OnBlockDestroyed: %s, Type: %d"),
                *BlockWorldLocation.ToString(), static_cast<int32>(BlockType)));

        // OnBlockDestroyed event'i SADECE server tarafında çağrılır
        OnBlockDestroyed.Broadcast(BlockWorldLocation, BlockType, BlockItemName, Damage, DamageInstigator, DamageCauser, DamageType);

        LogDebugMessage(EDebugCategory::BlockPlacement,
            FString::Printf(TEXT("SERVER - BLOCK DESTROYED: %s, Type: %d, Chunk: (%d,%d), Block: (%d,%d,%d)"),
                *BlockWorldLocation.ToString(), static_cast<int32>(BlockType),
                ChunkCoord.X, ChunkCoord.Y, BlockPos.X, BlockPos.Y, BlockPos.Z));

        // Blok kırılma anını görselleştir (daha büyük bir kutu çiz) - UPDATED
        DrawDebugBoxIfEnabled(EDebugCategory::BlockPlacement, BlockWorldLocation,
            FVector(BlockSize / 2.0f), FColor::Red);

        // *** UPDATED: Use chunk-based ISM removal instead of global search ***
        RemoveBlockInstance(ChunkCoord, BlockPos, BlockType);

        // Veri güncelleme
        SetBlockInternalWithoutReplication(ChunkCoord, BlockPos, EBlockType::Air);
        BlockDamageData.Remove(Key);

        // CLIENT'LARA BİLDİR
        MulticastUpdateBlock(ChunkCoord, BlockPos, EBlockType::Air);
    }

    // Bloğun yok edilip edilmediğini dön
    return bIsBlockDestroyed;
}

void ARandomMapGenerator::MulticastBlockDamaged_Implementation(const FChunkCoord& ChunkCoord, const FBlockPosition& BlockPos, float NewHealth, AActor* DamageInstigator, AActor* DamageCauser, TSubclassOf<UDamageType> DamageType)
{
    // Blok koordinatlarını oluştur
    FWorldBlockKey Key(ChunkCoord, BlockPos);
    // Blok tipini al
    EBlockType BlockType = GetBlockInternal(ChunkCoord, BlockPos);
    // Hava bloğuna hasar uygulamaya gerek yok
    if (BlockType == EBlockType::Air)
        return;
    // Blok dünya konumunu hesapla - debug için gerekli
    FVector BlockWorldLocation = BlockToWorldPosition(ChunkCoord, BlockPos);
    // ÖNEMLİ: OnBlockDestroyed olayını SADECE SERVER tarafında çağır
    // Client tarafında ÇAĞIRMA
    // Debug için client tarafında görselleştirme ekle
    if (!HasAuthority()) // Sadece client tarafında
    {
        // Debug görselleştirme ekle
        DrawDebugSphere(GetWorld(), BlockWorldLocation, 10.0f, 8, FColor::Yellow, false, 1.0f);
        // Hasar durumunu gösteren metin
        FString HealthText = FString::Printf(TEXT("%.1f / %.1f"), NewHealth, 100.0f);
        DrawDebugString(GetWorld(), BlockWorldLocation + FVector(0, 0, 20), *HealthText, nullptr, FColor::White, 1.0f);
        UE_LOG(LogTemp, Display, TEXT("CLIENT: Block damaged at %s, new health: %.1f"),
            *BlockWorldLocation.ToString(), NewHealth);
    }
    // Hasar verisini güncelle
    FBlockDamageData* DamageData = BlockDamageData.Find(Key);
    if (!DamageData)
    {
        // Yeni hasar verisi oluştur
        float MaxHealth = 100.0f; // Default value
        if (BlockDataTable)
        {
            FString BlockTypeStr = UEnum::GetValueAsString(BlockType).Replace(TEXT("EBlockType::"), TEXT(""));
            FBlockData* BlockDataRow = BlockDataTable->FindRow<FBlockData>(FName(*BlockTypeStr), TEXT(""));
            if (BlockDataRow)
            {
                MaxHealth = BlockDataRow->Durability;
            }
        }
        BlockDamageData.Add(Key, FBlockDamageData(MaxHealth));
        DamageData = BlockDamageData.Find(Key);
    }
    // Yeni can değerini ayarla
    DamageData->CurrentHealth = NewHealth;
    DamageData->LastDamageInstigator = DamageInstigator;
    DamageData->LastDamageCauser = DamageCauser;
    DamageData->LastDamageType = DamageType;
    // Hasar delegatesi çağır - hem client hem de server'da çağrılabilir
    float Damage = DamageData->MaxHealth - NewHealth; // Yaklaşık hasar miktarı
    OnBlockDamaged.Broadcast(BlockWorldLocation, BlockType, GetItemNameForBlockType(BlockType), Damage, DamageInstigator, DamageCauser, DamageType);
    // Blok sağlığı sıfır veya daha az ise, client tarafında görselleştirme ekle
    // ama OnBlockDestroyed event'ini çağırma!
    if (DamageData->CurrentHealth <= 0.0f && !HasAuthority())
    {
        // Sadece client tarafında görselleştirme
        DrawDebugBox(GetWorld(), BlockWorldLocation, FVector(BlockSize / 2.0f),
            FQuat::Identity, FColor::Red, false, 5.0f);
        UE_LOG(LogTemp, Warning, TEXT("CLIENT: Block destroyed at %s (visual only, event on server)"),
            *BlockWorldLocation.ToString());
        // Hasar verisini temizle
        BlockDamageData.Remove(Key);
    }
}

FName ARandomMapGenerator::GetItemNameForBlockType(EBlockType BlockType) const
{
    if (!BlockDataTable)
        return NAME_None;
    FString BlockTypeStr = UEnum::GetValueAsString(BlockType).Replace(TEXT("EBlockType::"), TEXT(""));
    FBlockData* BlockDataRow = BlockDataTable->FindRow<FBlockData>(FName(*BlockTypeStr), TEXT(""));
    if (BlockDataRow)
        return BlockDataRow->ItemName;
    return NAME_None;
}

void ARandomMapGenerator::SpawnBaseCore()
{
    // Sadece server Base Core'u spawn etmeli!
    if (!HasAuthority())
    {
        UE_LOG(LogTemp, Display, TEXT("Client: SpawnBaseCore - sadece server spawn edecek"));
        return;
    }

    // Eğer zaten spawn edilmişse tekrar spawn etme
    if (SpawnedBaseCore)
    {
        UE_LOG(LogTemp, Warning, TEXT("Base Core zaten spawn edilmiş!"));
        return;
    }

    // Base Core BP kontrolü
    if (!BaseCoreBP)
    {
        UE_LOG(LogTemp, Warning, TEXT("BaseCoreBP belirlenmemiş! Base Core spawn edilemedi."));
        return;
    }

    // Haritanın ortasını hesapla
    int32 CenterChunkX = WorldSizeInChunks / 2;
    int32 CenterChunkY = WorldSizeInChunks / 2;
    FChunkCoord CenterChunk(CenterChunkX, CenterChunkY);

    // Chunk'ın ortasındaki blok koordinatını hesapla
    int32 CenterBlockX = ChunkSize / 2;
    int32 CenterBlockY = ChunkSize / 2;

    // Haritanın en yüksek noktasını bul
    int32 CenterWorldX = CenterChunkX * ChunkSize + CenterBlockX;
    int32 CenterWorldY = CenterChunkY * ChunkSize + CenterBlockY;
    int32 TerrainHeight = GetTerrainHeight(CenterWorldX, CenterWorldY);

    UE_LOG(LogTemp, Display, TEXT("Base Core spawn - Merkez: (%d,%d), Terrain Height: %d"),
        CenterWorldX, CenterWorldY, TerrainHeight);

    // Base Core alanını düzleştir ve temizle
    for (int32 OffsetX = -BaseCoreSize; OffsetX <= BaseCoreSize; OffsetX++)
    {
        for (int32 OffsetY = -BaseCoreSize; OffsetY <= BaseCoreSize; OffsetY++)
        {
            int32 WorldX = CenterWorldX + OffsetX;
            int32 WorldY = CenterWorldY + OffsetY;

            // Chunk koordinatlarını hesapla
            FChunkCoord BlockChunk = FChunkCoord(
                FMath::FloorToInt(static_cast<float>(WorldX) / ChunkSize),
                FMath::FloorToInt(static_cast<float>(WorldY) / ChunkSize)
            );

            int32 LocalBlockX = WorldX % ChunkSize;
            if (LocalBlockX < 0) LocalBlockX += ChunkSize;

            int32 LocalBlockY = WorldY % ChunkSize;
            if (LocalBlockY < 0) LocalBlockY += ChunkSize;

            // Mevcut alanı temizle (terrain height'ın üzerindeki blokları kaldır)
            for (int32 Z = TerrainHeight; Z < ChunkHeight; Z++)
            {
                FBlockPosition BlockPos(LocalBlockX, LocalBlockY, Z);
                EBlockType OldBlockType = GetBlockInternal(BlockChunk, BlockPos);

                if (OldBlockType != EBlockType::Air)
                {
                    // Mevcut instance'ı kaldır
                    RemoveBlockInstance(BlockChunk, BlockPos, OldBlockType);
                    // Veriyi temizle
                    SetBlockInternalWithoutReplication(BlockChunk, BlockPos, EBlockType::Air);
                }
            }

            // Düz zemin seviyesini garantile (sadece eksik blokları doldur)
            for (int32 Z = 0; Z < TerrainHeight; Z++)
            {
                FBlockPosition BlockPos(LocalBlockX, LocalBlockY, Z);
                EBlockType CurrentBlockType = GetBlockInternal(BlockChunk, BlockPos);

                // Eğer blok yoksa (Air ise), yeni blok ekle
                if (CurrentBlockType == EBlockType::Air)
                {
                    EBlockType NewBlockType;
                    if (Z == TerrainHeight - 1)
                    {
                        // En üst katman Grass olsun (Base Core'un üzerinde durması için)
                        NewBlockType = EBlockType::Grass;
                    }
                    else if (Z >= TerrainHeight - 3)
                    {
                        // Üst katmanlar Dirt
                        NewBlockType = EBlockType::Dirt;
                    }
                    else
                    {
                        // Alt katmanlar Stone
                        NewBlockType = EBlockType::Stone;
                    }

                    SetBlockInternalWithoutReplication(BlockChunk, BlockPos, NewBlockType);
                    UpdateBlockInstance(BlockChunk, BlockPos, NewBlockType);
                }
            }
        }
    }

    // DÜZELTME: Base Core'un spawn konumunu doğru hesapla
    // En üst blok seviyesinin tam üzerine yerleştir
    FVector SpawnLocation = BlockToWorldPosition(CenterChunk, FBlockPosition(CenterBlockX, CenterBlockY, TerrainHeight - 1));

    // BlockToWorldPosition bloğun merkezini döndürür, üst yüzeyine çıkmak için tam BlockSize/2 + biraz ekstra
    SpawnLocation.Z += (BlockSize / 2.0f) + (BlockSize * 0.2f); // Bloğun üst yüzeyi + 20% güvenlik mesafesi

    UE_LOG(LogTemp, Display, TEXT("Base Core spawn location: %s"), *SpawnLocation.ToString());

    // Base Core'u spawn et
    FActorSpawnParameters SpawnParams;
    SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;

    SpawnedBaseCore = GetWorld()->SpawnActor<AActor>(BaseCoreBP, SpawnLocation, FRotator::ZeroRotator, SpawnParams);

    if (SpawnedBaseCore)
    {
        UE_LOG(LogTemp, Display, TEXT("Base Core başarıyla spawn edildi: %s, Pozisyon: %s"),
            *SpawnedBaseCore->GetName(), *SpawnedBaseCore->GetActorLocation().ToString());

        // Nav Mesh'i etkilemesini sağla
        UStaticMeshComponent* MeshComp = SpawnedBaseCore->FindComponentByClass<UStaticMeshComponent>();
        if (MeshComp)
        {
            MeshComp->SetCanEverAffectNavigation(true);
        }
    }
    else
    {
        UE_LOG(LogTemp, Error, TEXT("Base Core spawn edilemedi!"));
    }
}

void ARandomMapGenerator::GenerateDebugWalls()
{
    if (!SpawnedBaseCore)
    {
        UE_LOG(LogTemp, Warning, TEXT("Base Core spawn edilmediği için debug duvarları oluşturulamadı."));
        return;
    }

    // Base Core'un konumunu al
    FVector BaseCoreLocation = SpawnedBaseCore->GetActorLocation();

    // Dünya koordinatlarından chunk ve blok koordinatlarına dönüştür
    FChunkCoord CenterChunk = WorldToChunkCoord(BaseCoreLocation);
    FBlockPosition CenterBlock = WorldToBlockPosition(BaseCoreLocation);

    // Haritanın merkezi
    int32 CenterWorldX = CenterChunk.X * ChunkSize + CenterBlock.X;
    int32 CenterWorldY = CenterChunk.Y * ChunkSize + CenterBlock.Y;
    int32 LocalBaseHeight = WorldToBlockPosition(BaseCoreLocation).Z;

    UE_LOG(LogTemp, Display, TEXT("Debug duvarları oluşturuluyor. Base Core: %s, Merkez: (%d, %d), Yükseklik: %d"),
        *BaseCoreLocation.ToString(), CenterWorldX, CenterWorldY, LocalBaseHeight);

    // Çevreleyen duvarları oluştur
    int32 WallSize = BaseCoreSize + DebugWallDistance; // BaseCore'dan belirli uzaklıkta

    // Duvar oluştur
    for (int32 OffsetX = -WallSize; OffsetX <= WallSize; OffsetX++)
    {
        for (int32 OffsetY = -WallSize; OffsetY <= WallSize; OffsetY++)
        {
            // Sadece kenar bloklarını oluştur (çerçeve)
            if (abs(OffsetX) == WallSize || abs(OffsetY) == WallSize)
            {
                int32 WorldX = CenterWorldX + OffsetX;
                int32 WorldY = CenterWorldY + OffsetY;

                FChunkCoord BlockChunk = FChunkCoord(
                    FMath::FloorToInt(static_cast<float>(WorldX) / ChunkSize),
                    FMath::FloorToInt(static_cast<float>(WorldY) / ChunkSize)
                );

                int32 LocalBlockX = WorldX % ChunkSize;
                if (LocalBlockX < 0) LocalBlockX += ChunkSize;

                int32 LocalBlockY = WorldY % ChunkSize;
                if (LocalBlockY < 0) LocalBlockY += ChunkSize;

                // Debug duvarı oluştur
                for (int32 Z = 0; Z < DebugWallHeight; Z++)
                {
                    FBlockPosition BlockPos(LocalBlockX, LocalBlockY, LocalBaseHeight + Z);
                    SetBlockInternalWithoutReplication(BlockChunk, BlockPos, EBlockType::Stone);

                    // İkinci katman duvar (kalınlık için)
                    if (DebugWallThickness > 1)
                    {
                        int32 InnerOffsetX = (OffsetX < 0) ? 1 : (OffsetX > 0) ? -1 : 0;
                        int32 InnerOffsetY = (OffsetY < 0) ? 1 : (OffsetY > 0) ? -1 : 0;

                        int32 InnerWorldX = WorldX + InnerOffsetX;
                        int32 InnerWorldY = WorldY + InnerOffsetY;

                        FChunkCoord InnerBlockChunk = FChunkCoord(
                            FMath::FloorToInt(static_cast<float>(InnerWorldX) / ChunkSize),
                            FMath::FloorToInt(static_cast<float>(InnerWorldY) / ChunkSize)
                        );

                        int32 InnerLocalBlockX = InnerWorldX % ChunkSize;
                        if (InnerLocalBlockX < 0) InnerLocalBlockX += ChunkSize;

                        int32 InnerLocalBlockY = InnerWorldY % ChunkSize;
                        if (InnerLocalBlockY < 0) InnerLocalBlockY += ChunkSize;

                        FBlockPosition InnerBlockPos(InnerLocalBlockX, InnerLocalBlockY, LocalBaseHeight + Z);
                        SetBlockInternalWithoutReplication(InnerBlockChunk, InnerBlockPos, EBlockType::Stone);
                        UpdateBlockInstance(InnerBlockChunk, InnerBlockPos, EBlockType::Stone);
                    }

                    // Görsel güncelleme
                    UpdateBlockInstance(BlockChunk, BlockPos, EBlockType::Stone);
                }
            }
        }
    }

    UE_LOG(LogTemp, Display, TEXT("Debug duvarları başarıyla oluşturuldu."));
}

bool ARandomMapGenerator::IsNearBaseCore(int32 WorldX, int32 WorldY, int32 Distance)
{
    if (!SpawnedBaseCore)
        return false;

    FVector BaseCoreLocation = SpawnedBaseCore->GetActorLocation();
    FChunkCoord BaseCoreChunk = WorldToChunkCoord(BaseCoreLocation);
    FBlockPosition BaseCoreBlock = WorldToBlockPosition(BaseCoreLocation);

    int32 CoreWorldX = BaseCoreChunk.X * ChunkSize + BaseCoreBlock.X;
    int32 CoreWorldY = BaseCoreChunk.Y * ChunkSize + BaseCoreBlock.Y;

    int32 DiffX = abs(WorldX - CoreWorldX);
    int32 DiffY = abs(WorldY - CoreWorldY);

    return (DiffX <= Distance && DiffY <= Distance);
}

// *** DEBUG SPAWN POINTS - SADECE GÖRSELLEŞTİRME ***
void ARandomMapGenerator::GenerateSpawnPoints()
{
    // Bu fonksiyon artık sadece debug görselleştirme için kullanım
    // Gerçek spawn noktaları runtime'da GetEnemySpawnLocations/GetPlayerSpawnLocations ile alınıyor
    // HİÇBİR YENİ SPAWN POINT HESAPLANMAZ - SADECE GÖRSELLEŞTİRME!

    LogDebugMessage(EDebugCategory::WorldGeneration, TEXT("Debug spawn points visualization only - no computation"));

    // Debug görselleştirme için örnek konumlar oluştur (hesaplama yapmadan)
    float EffectiveBlockSize = BlockSize + BlockSpacing;
    float WorldSizeBlocks = WorldSizeInChunks * ChunkSize;
    float OffsetFromEdge = FMath::Max(3.0f, EnemySpawnDistanceFromEdge);

    // Sadece 4 köşe konumu hesapla (debug görselleştirme için)
    TArray<FVector> DebugEnemySpawns;

    int32 TopLeftX = FMath::FloorToInt(OffsetFromEdge);
    int32 TopLeftY = FMath::FloorToInt(OffsetFromEdge);
    int32 TopRightX = FMath::FloorToInt(WorldSizeBlocks - OffsetFromEdge);
    int32 TopRightY = FMath::FloorToInt(OffsetFromEdge);
    int32 BottomLeftX = FMath::FloorToInt(OffsetFromEdge);
    int32 BottomLeftY = FMath::FloorToInt(WorldSizeBlocks - OffsetFromEdge);
    int32 BottomRightX = FMath::FloorToInt(WorldSizeBlocks - OffsetFromEdge);
    int32 BottomRightY = FMath::FloorToInt(WorldSizeBlocks - OffsetFromEdge);

    // Yükseklikleri hesapla
    int32 TopLeftHeight = GetTerrainHeight(TopLeftX, TopLeftY);
    int32 TopRightHeight = GetTerrainHeight(TopRightX, TopRightY);
    int32 BottomLeftHeight = GetTerrainHeight(BottomLeftX, BottomLeftY);
    int32 BottomRightHeight = GetTerrainHeight(BottomRightX, BottomRightY);

    // Debug görselleştirme konumları
    TArray<FVector> DebugLocations = {
        FVector(TopLeftX * EffectiveBlockSize + (EffectiveBlockSize / 2.0f),
                TopLeftY * EffectiveBlockSize + (EffectiveBlockSize / 2.0f),
                (TopLeftHeight + 1) * EffectiveBlockSize),
        FVector(TopRightX * EffectiveBlockSize + (EffectiveBlockSize / 2.0f),
                TopRightY * EffectiveBlockSize + (EffectiveBlockSize / 2.0f),
                (TopRightHeight + 1) * EffectiveBlockSize),
        FVector(BottomLeftX * EffectiveBlockSize + (EffectiveBlockSize / 2.0f),
                BottomLeftY * EffectiveBlockSize + (EffectiveBlockSize / 2.0f),
                (BottomLeftHeight + 1) * EffectiveBlockSize),
        FVector(BottomRightX * EffectiveBlockSize + (EffectiveBlockSize / 2.0f),
                BottomRightY * EffectiveBlockSize + (EffectiveBlockSize / 2.0f),
                (BottomRightHeight + 1) * EffectiveBlockSize)
    };

    // Sadece görselleştirme - array'lere kaydetme
    for (const FVector& SpawnLoc : DebugLocations)
    {
        DrawDebugSphereIfEnabled(EDebugCategory::WorldGeneration, SpawnLoc,
            EffectiveBlockSize * 0.5f, FColor::Red, true);

        LogDebugMessage(EDebugCategory::WorldGeneration,
            FString::Printf(TEXT("Debug - Örnek düşman spawner konumu: %s"), *SpawnLoc.ToString()));
    }

    LogDebugMessage(EDebugCategory::WorldGeneration,
        FString::Printf(TEXT("Debug spawn noktaları sadece görselleştirme amacıyla gösterildi - %d konum"),
            DebugLocations.Num()));
}

// *** UPDATED: CAVE SPAWN SYSTEM INTEGRATION ***
TArray<FVector> ARandomMapGenerator::GetEnemySpawnLocations(int32 RequestedSpawnerCount) const
{
    TArray<FVector> SpawnLocations;

    // Eğer mağaralarda spawn etme aktifse ve mağaralar varsa
    if (bSpawnEnemiesInCaves && CaveLocations.Num() > 0)
    {
        UE_LOG(LogTemp, Warning, TEXT("Using cave spawn locations - %d caves available"), CaveLocations.Num());

        // Mağara sayısı ve istenen spawner sayısına göre spawn noktaları belirle
        int32 AvailableCaves = CaveLocations.Num();
        int32 SpawnersToCreate = FMath::Min(RequestedSpawnerCount, AvailableCaves);

        // İlk N mağarayı kullan (edge sırasına göre)
        for (int32 i = 0; i < SpawnersToCreate; i++)
        {
            SpawnLocations.Add(CaveLocations[i].CaveSpawnLocation);

            UE_LOG(LogTemp, Warning, TEXT("Cave spawn %d: Edge %d, Location: %s"),
                i, CaveLocations[i].EdgeIndex, *CaveLocations[i].CaveSpawnLocation.ToString());
        }

        // Eğer istenen spawn sayısı mağara sayısından fazlaysa, geri kalan spawn'ları mağara yakınlarına yerleştir
        if (RequestedSpawnerCount > AvailableCaves)
        {
            int32 RemainingSpawns = RequestedSpawnerCount - AvailableCaves;
            UE_LOG(LogTemp, Warning, TEXT("Need %d additional spawns near caves"), RemainingSpawns);

            // Mağara çevresinde ek spawn noktaları oluştur
            for (int32 i = 0; i < RemainingSpawns; i++)
            {
                // Mevcut mağaralardan birinin yakınında spawn noktası oluştur
                int32 CaveIndex = i % AvailableCaves;
                FVector CaveLocation = CaveLocations[CaveIndex].CaveSpawnLocation;

                // Mağara çevresinde rastgele offset
                float RandomAngle = i * (2.0f * PI / RemainingSpawns);
                float Distance = (BlockSize + BlockSpacing) * 3.0f; // 3 blok uzaklık

                FVector NearCaveSpawn = CaveLocation + FVector(
                    Distance * FMath::Cos(RandomAngle),
                    Distance * FMath::Sin(RandomAngle),
                    0.0f
                );

                SpawnLocations.Add(NearCaveSpawn);
            }
        }
    }
    else
    {
        // Mağara spawn'ı kapalıysa, eski sistemi kullan
        UE_LOG(LogTemp, Warning, TEXT("Using traditional edge spawn locations"));
        return GenerateEnemySpawnPoints(FMath::Clamp(RequestedSpawnerCount, 1, 16));
    }

    return SpawnLocations;
}

TArray<FVector> ARandomMapGenerator::GetPlayerSpawnLocations(int32 RequestedSpawnCount) const
{
    return GeneratePlayerSpawnPoints(FMath::Clamp(RequestedSpawnCount, 1, 32)); // En az 1, en fazla 32 player spawn
}

TArray<FVector> ARandomMapGenerator::GetEnemySpawnLocationsInRadius(int32 RequestedSpawnerCount, float MinDistanceFromCenter, float MaxDistanceFromCenter) const
{
    TArray<FVector> SpawnLocations;

    if (RequestedSpawnerCount <= 0)
        return SpawnLocations;

    // Dünya boyutlarını hesapla
    float EffectiveBlockSize = BlockSize + BlockSpacing;
    float WorldSizeBlocks = WorldSizeInChunks * ChunkSize;
    FVector MapCenter = FVector(
        (WorldSizeBlocks / 2.0f) * EffectiveBlockSize,
        (WorldSizeBlocks / 2.0f) * EffectiveBlockSize,
        0.0f
    );

    // İstenen sayıda spawn noktası oluştur
    for (int32 i = 0; i < RequestedSpawnerCount; i++)
    {
        // Daire üzerinde eşit açılarla dağıt
        float Angle = i * (2.0f * PI / RequestedSpawnerCount);

        // Min ve Max mesafe arasında rastgele mesafe seç
        float Distance = FMath::RandRange(MinDistanceFromCenter, MaxDistanceFromCenter);

        // Spawn konumunu hesapla
        float X = MapCenter.X + Distance * FMath::Cos(Angle);
        float Y = MapCenter.Y + Distance * FMath::Sin(Angle);

        // Blok koordinatlarına dönüştür
        int32 BlockX = FMath::FloorToInt(X / EffectiveBlockSize);
        int32 BlockY = FMath::FloorToInt(Y / EffectiveBlockSize);

        // Yüksekliği al
        int32 TerrainHeightBlock = GetTerrainHeight(BlockX, BlockY);
        float Z = (TerrainHeightBlock + 1) * EffectiveBlockSize;

        SpawnLocations.Add(FVector(X, Y, Z));
    }

    return SpawnLocations;
}

TArray<FVector> ARandomMapGenerator::GetPlayerSpawnLocationsAtDistance(int32 RequestedSpawnCount, float DistanceFromBaseCore) const
{
    return GeneratePlayerSpawnPoints(RequestedSpawnCount, DistanceFromBaseCore);
}

TArray<FVector> ARandomMapGenerator::GenerateEnemySpawnPoints(int32 Count) const
{
    TArray<FVector> SpawnLocations;

    if (Count <= 0)
        return SpawnLocations;

    // Dünya boyutlarını hesapla
    float EffectiveBlockSize = BlockSize + BlockSpacing;
    float WorldSizeBlocks = WorldSizeInChunks * ChunkSize;

    // Kenardan uzaklık
    float OffsetFromEdge = FMath::Max(3.0f, EnemySpawnDistanceFromEdge);

    if (Count <= 4)
    {
        // 4 veya daha az spawner isteniyorsa - köşelerde yerleştir
        TArray<FVector> CornerPositions;

        // 4 köşe konumu hesapla
        int32 TopLeftX = FMath::FloorToInt(OffsetFromEdge);
        int32 TopLeftY = FMath::FloorToInt(OffsetFromEdge);

        int32 TopRightX = FMath::FloorToInt(WorldSizeBlocks - OffsetFromEdge);
        int32 TopRightY = FMath::FloorToInt(OffsetFromEdge);

        int32 BottomLeftX = FMath::FloorToInt(OffsetFromEdge);
        int32 BottomLeftY = FMath::FloorToInt(WorldSizeBlocks - OffsetFromEdge);

        int32 BottomRightX = FMath::FloorToInt(WorldSizeBlocks - OffsetFromEdge);
        int32 BottomRightY = FMath::FloorToInt(WorldSizeBlocks - OffsetFromEdge);

        // Yükseklikleri hesapla
        int32 TopLeftHeight = GetTerrainHeight(TopLeftX, TopLeftY);
        int32 TopRightHeight = GetTerrainHeight(TopRightX, TopRightY);
        int32 BottomLeftHeight = GetTerrainHeight(BottomLeftX, BottomLeftY);
        int32 BottomRightHeight = GetTerrainHeight(BottomRightX, BottomRightY);

        // Köşe konumlarını diziye ekle
        CornerPositions.Add(FVector(
            TopLeftX * EffectiveBlockSize + (EffectiveBlockSize / 2.0f),
            TopLeftY * EffectiveBlockSize + (EffectiveBlockSize / 2.0f),
            (TopLeftHeight + 1) * EffectiveBlockSize
        ));

        CornerPositions.Add(FVector(
            TopRightX * EffectiveBlockSize + (EffectiveBlockSize / 2.0f),
            TopRightY * EffectiveBlockSize + (EffectiveBlockSize / 2.0f),
            (TopRightHeight + 1) * EffectiveBlockSize
        ));

        CornerPositions.Add(FVector(
            BottomLeftX * EffectiveBlockSize + (EffectiveBlockSize / 2.0f),
            BottomLeftY * EffectiveBlockSize + (EffectiveBlockSize / 2.0f),
            (BottomLeftHeight + 1) * EffectiveBlockSize
        ));

        CornerPositions.Add(FVector(
            BottomRightX * EffectiveBlockSize + (EffectiveBlockSize / 2.0f),
            BottomRightY * EffectiveBlockSize + (EffectiveBlockSize / 2.0f),
            (BottomRightHeight + 1) * EffectiveBlockSize
        ));

        // İstenen sayı kadar köşe konumu seç
        for (int32 i = 0; i < Count && i < CornerPositions.Num(); i++)
        {
            SpawnLocations.Add(CornerPositions[i]);
        }
    }
    else
    {
        // 4'ten fazla spawner isteniyorsa - harita kenarlarında dağıt
        FVector MapCenter = FVector(
            (WorldSizeBlocks / 2.0f) * EffectiveBlockSize,
            (WorldSizeBlocks / 2.0f) * EffectiveBlockSize,
            0.0f
        );

        // Merkeze olan mesafeyi hesapla (haritanın kenarlarında yerleştirmek için)
        float DistanceFromCenter = (WorldSizeBlocks / 2.0f - OffsetFromEdge) * EffectiveBlockSize;

        for (int32 i = 0; i < Count; i++)
        {
            // Daire üzerinde eşit açılarla dağıt
            float Angle = i * (2.0f * PI / Count);

            // Biraz varyasyon ekle (±10% mesafe varyasyonu)
            float RandomFactor = FMath::RandRange(0.9f, 1.1f);
            float Distance = DistanceFromCenter * RandomFactor;

            float X = MapCenter.X + Distance * FMath::Cos(Angle);
            float Y = MapCenter.Y + Distance * FMath::Sin(Angle);

            // Blok koordinatlarına dönüştür
            int32 BlockX = FMath::FloorToInt(X / EffectiveBlockSize);
            int32 BlockY = FMath::FloorToInt(Y / EffectiveBlockSize);

            // Harita sınırları içinde tut
            BlockX = FMath::Clamp(BlockX, (int32)OffsetFromEdge, (int32)(WorldSizeBlocks - OffsetFromEdge));
            BlockY = FMath::Clamp(BlockY, (int32)OffsetFromEdge, (int32)(WorldSizeBlocks - OffsetFromEdge));

            // Yüksekliği al
            int32 TerrainHeightBlock = GetTerrainHeight(BlockX, BlockY);

            // Final koordinatları hesapla
            FVector SpawnLocation = FVector(
                BlockX * EffectiveBlockSize + (EffectiveBlockSize / 2.0f),
                BlockY * EffectiveBlockSize + (EffectiveBlockSize / 2.0f),
                (TerrainHeightBlock + 1) * EffectiveBlockSize
            );

            SpawnLocations.Add(SpawnLocation);
        }
    }

    return SpawnLocations;
}

TArray<FVector> ARandomMapGenerator::GeneratePlayerSpawnPoints(int32 Count, float Distance) const
{
    TArray<FVector> SpawnLocations;

    if (Count <= 0 || !SpawnedBaseCore)
        return SpawnLocations;

    FVector BaseCoreLocation = SpawnedBaseCore->GetActorLocation();
    float EffectiveBlockSize = BlockSize + BlockSpacing;

    // Mesafe belirtilmemişse varsayılan mesafeyi kullan
    float SpawnDistance = (Distance > 0.0f) ? Distance : (3.0f * EffectiveBlockSize);

    // İstenen sayıda spawn noktası oluştur (daire şeklinde)
    for (int32 i = 0; i < Count; i++)
    {
        float Angle = i * (2.0f * PI / Count);

        // Biraz rastgelelik ekle (±20% mesafe varyasyonu)
        float RandomFactor = FMath::RandRange(0.8f, 1.2f);
        float ActualDistance = SpawnDistance * RandomFactor;

        float X = BaseCoreLocation.X + ActualDistance * FMath::Cos(Angle);
        float Y = BaseCoreLocation.Y + ActualDistance * FMath::Sin(Angle);

        // Blok koordinatlarına dönüştür
        int32 BlockX = FMath::FloorToInt(X / EffectiveBlockSize);
        int32 BlockY = FMath::FloorToInt(Y / EffectiveBlockSize);

        // Bu konumdaki arazi yüksekliğini al
        int32 TerrainHeightBlock = GetTerrainHeight(BlockX, BlockY);

        // Z'yi arazinin hemen üzerine ayarla
        float Z = (TerrainHeightBlock + 1) * EffectiveBlockSize;

        // Oyuncu spawn konumlarına ekle
        FVector SpawnPosition = FVector(X, Y, Z);
        SpawnLocations.Add(SpawnPosition);
    }

    return SpawnLocations;
}

bool ARandomMapGenerator::SwapRemoveInstance(UInstancedStaticMeshComponent* ChunkISM, FChunkISMData& ChunkData, const FBlockTypePositionKey& KeyToRemove)
{
    int32* FoundIndex = ChunkData.InstanceIndexMapping.Find(KeyToRemove);
    if (!FoundIndex) return false;

    int32 RemoveIndex = *FoundIndex;
    int32 LastIndex = ChunkISM->GetInstanceCount() - 1;

    if (RemoveIndex != LastIndex)
    {
        // Son instance ile swap yap
        FTransform LastTransform;
        ChunkISM->GetInstanceTransform(LastIndex, LastTransform, true);
        ChunkISM->UpdateInstanceTransform(RemoveIndex, LastTransform, true, true);

        // Mapping'i düzelt
        FBlockTypePositionKey LastKey;
        for (auto& Pair : ChunkData.InstanceIndexMapping)
        {
            if (Pair.Value == LastIndex)
            {
                LastKey = Pair.Key;
                break;
            }
        }

        ChunkData.InstanceIndexMapping[LastKey] = RemoveIndex;
    }

    ChunkISM->RemoveInstance(LastIndex);
    ChunkData.InstanceIndexMapping.Remove(KeyToRemove);

    return true;
}


void ARandomMapGenerator::InitializeDebugSystem()
{
    if (!DebugManager)
    {
        DebugManager = UDebugManager::FindDebugManager(GetWorld());
    }

    if (DebugManager)
    {
        LogDebugMessage(EDebugCategory::WorldGeneration, TEXT("Debug system initialized for MapGenerator"));
    }
}

void ARandomMapGenerator::DrawDebugSphereIfEnabled(EDebugCategory Category, const FVector& Center, float Radius, const FColor& Color, bool bPersistent)
{
    if (DebugManager)
    {
        DebugManager->DrawDebugSphereIfEnabled(Category, Center, Radius, Color, bPersistent);
    }
}

void ARandomMapGenerator::DrawDebugLineIfEnabled(EDebugCategory Category, const FVector& Start, const FVector& End, const FColor& Color, bool bPersistent)
{
    if (DebugManager)
    {
        DebugManager->DrawDebugLineIfEnabled(Category, Start, End, Color, bPersistent);
    }
}

void ARandomMapGenerator::DrawDebugBoxIfEnabled(EDebugCategory Category, const FVector& Center, const FVector& Extent, const FColor& Color, bool bPersistent)
{
    if (DebugManager)
    {
        DebugManager->DrawDebugBoxIfEnabled(Category, Center, Extent, Color, bPersistent);
    }
}

void ARandomMapGenerator::LogDebugMessage(EDebugCategory Category, const FString& Message, bool bWarning)
{
    if (DebugManager)
    {
        DebugManager->PrintDebugLog(Category, Message, bWarning);
    }
}