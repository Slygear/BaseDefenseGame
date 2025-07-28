#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ProceduralMeshComponent.h"
#include "Engine/DataTable.h"
#include "Net/UnrealNetwork.h"
#include "ARandomMapGenerator.generated.h"

UENUM(BlueprintType)
enum class EBlockType : uint8
{
    Air,
    Grass,
    Dirt,
    Stone,
    Wood,
    Leaves,
    Turret,
    Trap,
    Production,
    Storage,
    InvisibleWall,
    MAX
};

USTRUCT(BlueprintType)
struct FBlockData : public FTableRowBase
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    EBlockType BlockType;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    bool bIsFunctionalBlock = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (EditCondition = "!bIsFunctionalBlock"))
    FVector2D TopTile;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (EditCondition = "!bIsFunctionalBlock"))
    FVector2D SideTile;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (EditCondition = "!bIsFunctionalBlock"))
    FVector2D BottomTile;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (EditCondition = "bIsFunctionalBlock"))
    TSubclassOf<AActor> ActorClass;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float Durability = 100.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FName ItemName;
};

USTRUCT(BlueprintType)
struct FBlock
{
    GENERATED_BODY()
    UPROPERTY() EBlockType Type = EBlockType::Air;
};

USTRUCT(BlueprintType)
struct FChunk
{
    GENERATED_BODY()

    UPROPERTY() UProceduralMeshComponent* Mesh = nullptr;
    UPROPERTY() TArray<FBlock> Blocks;
    UPROPERTY() bool bNeedsRebuild = false;
};

USTRUCT(BlueprintType)
struct FBlockDamageData
{
    GENERATED_BODY()
    UPROPERTY() float CurrentHealth;
    UPROPERTY() float MaxHealth;
    UPROPERTY() AActor* LastDamageInstigator;
    UPROPERTY() AActor* LastDamageCauser;
    UPROPERTY() TSubclassOf<UDamageType> LastDamageType;

    FBlockDamageData() : CurrentHealth(100.f), MaxHealth(100.f), LastDamageInstigator(nullptr), LastDamageCauser(nullptr), LastDamageType(nullptr) {}
    FBlockDamageData(float InMax) : CurrentHealth(InMax), MaxHealth(InMax), LastDamageInstigator(nullptr), LastDamageCauser(nullptr), LastDamageType(nullptr) {}
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_SevenParams(FOnBlockDamaged, const FVector&, Location, EBlockType, BlockType, FName, ItemName, float, Damage, AActor*, DamageInstigator, AActor*, DamageCauser, TSubclassOf<UDamageType>, DamageType);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_SevenParams(FOnBlockDestroyed, const FVector&, Location, EBlockType, BlockType, FName, ItemName, float, Damage, AActor*, DamageInstigator, AActor*, DamageCauser, TSubclassOf<UDamageType>, DamageType);

UCLASS()
class BASEDEFENSE_API ARandomMapGenerator : public AActor
{
    GENERATED_BODY()

public:
    ARandomMapGenerator();

    virtual void BeginPlay() override;
    virtual void Tick(float DeltaSeconds) override;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

    // === World settings ===
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "World") int32 WorldSizeInChunks = 8;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "World") int32 ChunkSize = 16;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "World") int32 ChunkHeight = 64;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "World") float BlockSize = 100.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "World") float MapFlatness = 0.5f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "World") float TreeDensity = 0.5f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "World") int32 BaseHeight = 10;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "World") float HeightVariation = 5.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "World") float NoiseScale = 0.1f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Blocks") UDataTable* BlockDataTable;

    // Atlas settings
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TextureAtlas") int32 AtlasCols = 3;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TextureAtlas") int32 AtlasRows = 2;

    // Delegates
    UPROPERTY(BlueprintAssignable) FOnBlockDamaged OnBlockDamaged;
    UPROPERTY(BlueprintAssignable) FOnBlockDestroyed OnBlockDestroyed;

    // === Functions ===
    UFUNCTION(BlueprintCallable) void GenerateWorld();
    UFUNCTION(BlueprintCallable) void CreateChunk(const FIntPoint& Coord);
    UFUNCTION(BlueprintCallable) void RebuildChunk(const FIntPoint& ChunkCoord);
    UFUNCTION(BlueprintCallable) void GenerateTree(int32 WorldX, int32 WorldY, int32 WorldZ, FChunk& Chunk);

    UFUNCTION(BlueprintCallable) void GenerateMountainBorderSystem();
    UFUNCTION(BlueprintCallable) void GenerateCaveSystem();

    UFUNCTION(BlueprintCallable) void SpawnBaseCore();
    UFUNCTION(BlueprintCallable) void GenerateSpawnPoints();

    UFUNCTION(BlueprintCallable) void SetBlock(const FIntPoint& ChunkCoord, int32 X, int32 Y, int32 Z, EBlockType NewType);
    UFUNCTION(BlueprintCallable) EBlockType GetBlock(const FIntPoint& ChunkCoord, int32 X, int32 Y, int32 Z) const;

    UFUNCTION(BlueprintCallable) void SetBlockTypeAtPosition(const FVector& WorldLocation, EBlockType BlockType);
    UFUNCTION(BlueprintCallable) EBlockType GetBlockTypeAtPosition(const FVector& WorldLocation) const;

    UFUNCTION(BlueprintCallable) bool ApplyDamageToBlock(const FVector& WorldLocation, float Damage, AActor* EventInstigator = nullptr, AActor* DamageCauser = nullptr, TSubclassOf<UDamageType> DamageType = nullptr);

    UFUNCTION(NetMulticast, Reliable) void MulticastBlockChanged(const FIntPoint& ChunkCoord, int32 X, int32 Y, int32 Z, EBlockType NewType);
    UFUNCTION(NetMulticast, Reliable) void MulticastBlockDamaged(const FChunkCoord& ChunkCoord, const FBlockPosition& BlockPos, float NewHealth, AActor* DamageInstigator, AActor* DamageCauser, TSubclassOf<UDamageType> DamageType);

protected:
    UPROPERTY() TMap<FIntPoint, FChunk> Chunks;
    UPROPERTY() TMap<FWorldBlockKey, FBlockDamageData> BlockDamageData;

    void AddCubeFaces(TArray<FVector>& Vertices, TArray<int32>& Triangles, TArray<FVector2D>& UVs, FVector WorldPos, const FBlockData& Data);
    FVector2D GetTileUV(const FVector2D& Tile, int32 CornerIndex) const;

    void ClearGeneratorState();
    float GetPerlinNoise(float X, float Y) const;
};
