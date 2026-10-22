// Copyright Epic Games, Inc. All Rights Reserved.

#include "Dune/DuneHUD.h"
#include "Dune/DuneWeaponComponent.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "GameFramework/PlayerController.h"

void ADuneHUD::DrawHUD()
{
	Super::DrawHUD();
	const APawn* Pawn = PlayerOwner ? PlayerOwner->GetPawn() : nullptr;
	const UDuneWeaponComponent* Weapons = Pawn ? Pawn->FindComponentByClass<UDuneWeaponComponent>() : nullptr;
	if (!Canvas || !Weapons)
	{
		return;
	}
	const bool bGun = Weapons->GetWeapon() == EDuneWeapon::Gun;
	if (bGun)
	{
		// the screen centre is where the bullet is aimed (the gun traces from the camera through this point)
		const float X = Canvas->SizeX * 0.5f;
		const float Y = Canvas->SizeY * 0.5f;
		const FLinearColor Color(1.f, 0.9f, 0.2f, 1.f);
		DrawLine(X - 14.f, Y, X - 4.f, Y, Color, 2.f);
		DrawLine(X + 4.f, Y, X + 14.f, Y, Color, 2.f);
		DrawLine(X, Y - 14.f, X, Y - 4.f, Color, 2.f);
		DrawLine(X, Y + 4.f, X, Y + 14.f, Color, 2.f);
	}
	DrawText(bGun ? TEXT("GUN   [Tab] knife   [LMB] shoot") : TEXT("KNIFE   [Tab] gun   [LMB] slash"), FLinearColor::White, 24.f, Canvas->SizeY - 50.f, GEngine->GetMediumFont(), 1.f);
}
