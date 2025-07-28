// UBuildSystem.h - Fixed version with missing function declaration
#pragma once

#include "UDebugManager.h"
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameFramework/Actor.h"
#include "Engine/DataTable.h"
#include "ARandomMapGenerator.h"
#include "UBuildSystem.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnBlockPlaced, const FVector&, Location, EBlockType, BlockType, FName, ItemName);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnBlockRemoved, FVector, Location);

/**
 * Component for building/breaking blocks
 */
UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class BASEDEFENSE_API UBuildSystem : public UActorComponent
{
    GENERATED_BODY()

public:
    UBuildSystem();

    virtual void BeginPlay() override;
    virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

    // Reference to map generator
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Build System")
    TSoftObjectPtr<ARandomMapGenerator> MapGenerator;

    // Distance to trace for build placement
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Build System")
    float BuildDistance = 500.0f;

    // Block data table
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Build System")
    UDataTable* BlockDataTable;

    // Material for valid placement
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Build System")
    UMaterialInterface* ValidPlacementMaterial;

    // Material for invalid placement
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Build System")
    UMaterialInterface* InvalidPlacementMaterial;

    // Ghost block mesh components
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Build System")
    UStaticMeshComponent* GhostBlockMesh;

    // Current block type to place
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Replicated, Category = "Build System")
    EBlockType CurrentBlockType;

    // Whether build mode is active
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Replicated, Category = "Build System")
    bool bBuildModeActive;

    // Delegates
    UPROPERTY(BlueprintAssignable, Category = "Build System")
    FOnBlockPlaced OnBlockPlaced;

    UPROPERTY(BlueprintAssignable, Category = "Build System")
    FOnBlockRemoved OnBlockRemoved;

    // Activate build mode
    UFUNCTION(BlueprintCallable, Category = "Build System")
    void ActivateBuildMode(EBlockType BlockType);

    // Server RPC for activation
    UFUNCTION(Server, Reliable, WithValidation)
    void ServerActivateBuildMode(EBlockType BlockType);

    // Deactivate build mode
    UFUNCTION(BlueprintCallable, Category = "Build System")
    void DeactivateBuildMode();

    // Server RPC for deactivation
    UFUNCTION(Server, Reliable, WithValidation)
    void ServerDeactivateBuildMode();

    // Try to place block at current look location
    UFUNCTION(BlueprintCallable, Category = "Build System")
    bool TryPlaceBlock();

    // Try to remove block at current look location
    UFUNCTION(BlueprintCallable, Category = "Build System")
    bool TryRemoveBlock();

    // Check if a block can be placed at the specified location
    UFUNCTION(BlueprintCallable, Category = "Build System")
    bool CanPlaceBlockAt(const FVector& Location, EBlockType BlockType);

    // Place a special functional block (like turret)
    UFUNCTION(BlueprintCallable, Category = "Build System")
    AActor* PlaceFunctionalBlock(TSubclassOf<AActor> ActorClass, const FVector& Location, const FRotator& Rotation);

    // Şu anki yapı ID'si (row index, sıra numarası)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Replicated, Category = "Build System")
    int32 CurrentBuildRowIndex;

    // Şu anki yapının büyüklüğü
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Replicated, Category = "Build System")
    int32 CurrentBlockSize;

    // Row indeksi ile build modunu aktifleştir
    UFUNCTION(BlueprintCallable, Category = "Build System")
    void ActivateBuildModeByRowIndex(int32 RowIndex);

    // Server RPC for activation by row index
    UFUNCTION(Server, Reliable, WithValidation)
    void ServerActivateBuildModeByRowIndex(int32 RowIndex);

    // Ghost bloğun görünümünü güncelle (build modu deaktif etmeden mesh'i değiştir)
    UFUNCTION(BlueprintCallable, Category = "Build System")
    void ChangeMesh(int32 RowIndex);

    // Apply damage to a block (build system üzerinden)
    UFUNCTION(BlueprintCallable, Category = "Build System")
    bool ApplyDamageToBlock(const FVector& Location, float Damage, AActor* EventInstigator = nullptr, AActor* DamageCauser = nullptr, TSubclassOf<UDamageType> DamageTypeClass = nullptr);

    // Şu anki yapının ItemName değeri
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Build System")
    FName CurrentItemName;

    // ItemName değerini almak için yardımcı fonksiyon
    UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Build System")
    FName GetCurrentItemName() const;

    // BlokType'a göre ItemName almak için yardımcı fonksiyon
    UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Build System")
    FName GetItemNameForBlockType(EBlockType BlockType) const;

    // Row Index'e göre ItemName almak için yardımcı fonksiyon
    UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Build System")
    FName GetItemNameForRowIndex(int32 RowIndex) const;

    // Ghost bloğu döndürmek için fonksiyon
    UFUNCTION(BlueprintCallable, Category = "Build System")
    void RotateGhostBlock(float RotationDelta);

    UFUNCTION(BlueprintCallable, Category = "Build System")
    int32 GetRowIndexByItemName(const FName& SearchItemName) const;

    // Server RPC functions
    UFUNCTION(Server, Reliable, WithValidation)
    void ServerPlaceBlock(const FVector& Location, EBlockType BlockType);

    UFUNCTION(Server, Reliable, WithValidation)
    void ServerRemoveBlock(const FVector& Location);

    UFUNCTION(Server, Reliable, WithValidation)
    void ServerPlaceFunctionalBlock(TSubclassOf<AActor> ActorClass, const FVector& Location, const FRotator& Rotation);

    UFUNCTION(Server, Reliable, WithValidation)
    void ServerPlaceFunctionalBlockFromTable(TSubclassOf<AActor> ActorClass, const FVector& Location, const FRotator& Rotation, EBlockType BlockType, FName ItemName);

    UFUNCTION(Server, Reliable, WithValidation)
    void ServerApplyDamageToBlock(const FVector& Location, float Damage, AActor* EventInstigator, AActor* DamageCauser, TSubclassOf<UDamageType> DamageTypeClass);

    // Functional block tag ayarlamak için multicast fonksiyon
    UFUNCTION(NetMulticast, Reliable)
    void MulticastSetFunctionalBlockTag(AActor* Actor);

    // *** YENİ FONKSİYON: INVISIBLE WALL DETECTION ***
    UFUNCTION(BlueprintCallable, Category = "Build System")
    bool IsLocationBlockedByInvisibleWall(const FVector& Location);

    // === DEBUG SYSTEM INTEGRATION ===

    // Debug manager reference (Inspector'da görünür, manuel olarak set edebilirsiniz)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Debug System")
    UDebugManager* DebugManager;

    // Debug functions
    UFUNCTION(BlueprintCallable, Category = "Debug System")
    void InitializeDebugSystem();

    UFUNCTION(BlueprintCallable, Category = "Debug System")
    void ReinitializeAllDebugSystems();

protected:
    // Get player view point
    void GetPlayerViewPoint(FVector& Location, FRotator& Rotation) const;

    // Update ghost block position and validity
    void UpdateGhostBlock();

    // Get block size for current block type
    int32 GetCurrentBlockSize() const;

    // Find valid placement surface
    bool FindPlacementSurface(FVector& OutLocation, FVector& OutNormal);

private:
    // Whether we found a valid placement position this frame
    bool bHasValidPlacement;

    // Current ghost block location
    FVector GhostBlockLocation;

    // Current look direction
    FVector LookDirection;

    // Whether current block is functional
    bool bIsCurrentBlockFunctional;

    // Whether current block can be rotated
    bool bCanCurrentBlockRotate;

    // Belirli bir konumda functional blok var mı
    bool IsFunctionalBlockAt(const FVector& Location);

    // Belirli bir mesafe içinde functional blok var mı
    bool IsFunctionalBlockNearby(const FVector& Location, float Radius) const;

    bool IsFunctionalBlockNearCorner(const FVector& Location, float Radius) const;

    // Helper functions
    void DrawDebugSphereIfEnabled(EDebugCategory Category, const FVector& Center, float Radius, const FColor& Color, bool bPersistent = false);
    void DrawDebugLineIfEnabled(EDebugCategory Category, const FVector& Start, const FVector& End, const FColor& Color, bool bPersistent = false);
    void DrawDebugBoxIfEnabled(EDebugCategory Category, const FVector& Center, const FVector& Extent, const FColor& Color, bool bPersistent = false);
    void LogDebugMessage(EDebugCategory Category, const FString& Message, bool bWarning = false);
};