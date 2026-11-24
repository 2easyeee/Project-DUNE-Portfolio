#include "Dune/Combat/DuneHealthComponent.h"
#include "GameFramework/Actor.h"

UDuneHealthComponent::UDuneHealthComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UDuneHealthComponent::BeginPlay()
{
	Super::BeginPlay();
	if (Health <= 0.f) Health = MaxHealth;
	if (AActor* Owner = GetOwner()) Owner->OnTakeAnyDamage.AddDynamic(this, &UDuneHealthComponent::HandleAnyDamage);
}

void UDuneHealthComponent::Init(float InHealth, float InMax)
{
	MaxHealth = FMath::Max(1.f, InMax);
	Health = FMath::Clamp(InHealth, 0.f, MaxHealth);
	OnHealthChanged.Broadcast(Health, MaxHealth, 0.f);
}

void UDuneHealthComponent::Heal(float Amount)
{
	if (IsDead() || Amount <= 0.f) return;
	const float Old = Health;
	Health = FMath::Min(MaxHealth, Health + Amount);
	OnHealthChanged.Broadcast(Health, MaxHealth, Health - Old);
}

void UDuneHealthComponent::HandleAnyDamage(AActor*, float Damage, const UDamageType*, AController*, AActor* DamageCauser)
{
	if (IsDead() || Damage <= 0.f || bInvulnerable) return;
	bHasDamageFrom = DamageCauser != nullptr;
	if (DamageCauser && GetWorld())
	{
		LastDamageFrom = DamageCauser->GetActorLocation();
		LastDamageTime = GetWorld()->GetTimeSeconds();
	}
	const float Old = Health;
	Health = FMath::Max(0.f, Health - Damage);
	OnHealthChanged.Broadcast(Health, MaxHealth, Health - Old);
	if (IsDead()) OnDied.Broadcast();
}
