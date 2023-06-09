#include "Engine/Engine.h"
#include "Engine/ENGINE_EVENT.h"

#include "Components2D/Rendering/AnimatedSpritesRenderer.h"
#include "Objects2D/Physics/Colliders/AARectCollider.h"
#include "Objects2D/Physics/Colliders/RectCollider.h"

#include "PlayerScript.h"
#include "UIScript.h"
#include "MapRenderer.h"
#include "GameInit.h"
#include "DebugRenderer.h"
#include "GameUtils.h"
#include "TAG.h"
#include "COLLISION_MASK.h"
#include "ObjectGenerator.h"

using namespace soft;

void AddStaticObjects(Scene2D* scene, byte* mapValues, size_t width, size_t height,
	byte* blockCells, byte blockCellCount)
{
	bool isCellBlocked[256] = {};
	for (size_t i = 0; i < blockCellCount; i++)
	{
		isCellBlocked[blockCells[i]] = true;
	}

	auto cellCollider = MakeShared<AARectCollider>(AARect({ 0,0 }, { 60,60 }));
	for (size_t y = 0; y < height; y++)
	{
		auto row = &mapValues[y * width];
		for (size_t x = 0; x < width; x++)
		{
			//row[x] = 1;// Random::RangeInt64(1, 2);

			//auto v = Random::RangeInt64(1, 10);

			//if (y == 0 || y == height - 1 || x == 0 || x == width - 1 || v == 10)
			//{
			//	row[x] = 0;
			//	auto object = mheap::New<GameObject2D>(GameObject2D::STATIC);
			//	object->NewComponent<Physics2D>(cellCollider)
			//		->CollisionMask() = 1ull | (1ull << 2) | (1ull << 3);
			//	object->Position() = { x * 60, y * 60 };
			//	scene->AddObject(object);
			//}

			//if (v == 10 && !(y == 0 || y == height - 1 || x == 0 || x == width - 1))
			//{
			//	row[x] = 2;
			//}

			if (isCellBlocked[row[x]])
			{
				// add static object as blocked cell
				auto object = mheap::New<GameObject2D>(GameObject2D::STATIC);
				object->NewComponent<Physics2D>(cellCollider)
					->CollisionMask() = COLLISION_MASK::WALL;
				object->Position() = { x * 60, y * 60 };
				scene->AddObject(object);
			}
		}
	}
}

void AddPlayer(Scene2D* scene, ID id, const Vec2& pos, size_t width, size_t height)
{
	auto player = mheap::New<GameObject2D>(GameObject2D::DYNAMIC);
	player->Tag() = TAG::PLAYER;
	player->Position() = pos;
	auto sprites = player->NewComponent<SpritesRenderer>();
	sprites->SetSprite(sprites->Load(String::Format("Player_{}.png", id), AARect(), Vec2(50, 50)));
	sprites->Load(String::Format("PlayerUP_{}.png", id), AARect(), Vec2(50, 50));
	sprites->Load(String::Format("PlayerDOWN_{}.png", id), AARect(), Vec2(50, 50));
	sprites->Load(String::Format("PlayerLEFT_{}.png", id), AARect(), Vec2(50, 50));
	sprites->Load(String::Format("PlayerRIGHT_{}.png", id), AARect(), Vec2(50, 50));
	sprites->ClearAABB();

	auto script = player->NewComponent<PlayerScript>();
	script->SetUserId(id);
	Global::Get().players[id] = script.Get();

	auto cellCollider = MakeShared<AARectCollider>(AARect({ 0,0 }, { 50,50 }), Vec2(5, 5));
	player->NewComponent<RigidBody2D>(RigidBody2D::KINEMATIC, cellCollider)
		->CollisionMask() = COLLISION_MASK::PLAYER;


	if (Global::Get().userId == id)
	{
		Vec2 camViewSize = { 960 * 1.2f, 720 * 1.2f };
		auto camObj = mheap::New<GameObject2D>(GameObject2D::DYNAMIC);
		auto cam = camObj->NewComponent<Camera2D>(AARect({ 0,0 }, camViewSize));
		cam->SetClamp(camViewSize / 2.0f, Vec2(width * 60) - camViewSize / 2.0f);
		player->AddChild(camObj);
	}

	// red line
	auto line = mheap::New<GameObject2D>(GameObject2D::DYNAMIC);
	auto lineRdr = line->NewComponent<SpriteRenderer>("red.png");
	lineRdr->Sprite().FitTextureSize(Vec2(1, 5));
	lineRdr->Sprite().SetOpacity(64);
	lineRdr->ClearAABB();
	line->Position() = { 50 / 2.0f, 40 };
	player->AddChild(line);

	//// gun
	//auto gunObj = mheap::New<GameObject2D>(GameObject2D::DYNAMIC);
	//auto gunRdr = gunObj->NewComponent<SpriteRenderer>("smg2.png");
	//gunRdr->Sprite().SetAnchorPoint(Vec2(100 / 331.0f, 40 / 120.0f));
	//gunRdr->Sprite().Transform().Scale() = Vec2(0.3f);
	//gunRdr->ClearAABB();
	//gunObj->Position() = { 50 / 2.0f, 40 };
	//player->AddChild(gunObj);

	// crosshair
	auto crossHair = mheap::New<GameObject2D>(GameObject2D::DYNAMIC);
	auto crossHairRdr = crossHair->NewComponent<SpriteRenderer>("CrosshairsRed.png");
	crossHairRdr->Sprite().FitTextureSize({ 80, 80 });
	crossHairRdr->Sprite().SetAnchorPoint({ 0.5f,0.5f });
	crossHairRdr->Sprite().SetOpacity(128);
	crossHairRdr->ClearAABB();
	player->AddChild(crossHair);

	GameUtils::AddHpBar(player, script->GetTeamId() == Global::Get().GetMyTeamId() ? sf::Color::Green : sf::Color::Red, 
		70, 100, &script->DynamicObjectProperties().hp)->Position() = { -10, -20 };

	scene->AddObject(player);
}

void AddMapRenderer(Scene2D* scene, const byte* mapValues, size_t width, size_t height)
{
	auto map = mheap::New<GameObject2D>(GameObject2D::GHOST);
	auto mapRenderer = map->NewComponent<MapRenderer>(width, height, Vec2(60, 60), 10);

	for (size_t y = 0; y < height; y++)
	{
		auto row = &mapValues[y * width];
		for (size_t x = 0; x < width; x++)
		{
			mapRenderer->LoadCell(row[x], String::Format("{}.png", (size_t)row[x]), {});
			//mapRenderer->SetCellValue(x, y, row[x]);
		}
	}

	scene->AddObject(map);

	{
		auto object = mheap::New<GameObject2D>(GameObject2D::GHOST);
		object->Position() = { 200, 200 };
		object->Scale() = { 2.0f, 2.0f };

		auto rdr = object->NewComponent<AnimatedSpritesRenderer>();
		rdr->ClearAABB();

		ID spriteFrameIDs[12] = {};

		for (size_t i = 0; i < 12; i++)
		{
			spriteFrameIDs[i] = rdr->LoadSpriteFrame(
				String::Format("monsters/swordsman/monster_swordsman_run_{%04d}.png", i)
			);
		}

		auto animID = rdr->MakeAnimation(spriteFrameIDs, 12, 5.0f);
		rdr->SetAnimation(animID);
		scene->AddObject(object);
	}
}

void AddUINode(Scene2D* scene)
{
	auto ui = mheap::New<GameObject2D>(GameObject2D::GHOST);
	ui->NewComponent<UIScript>();
	ui->NewComponent<DebugRenderer>();
	scene->AddObject(ui);
}

void AddMapMonsters(Scene2D* scene, const byte* mapMonsterIds, size_t width, size_t height)
{
	for (size_t y = 0; y < height; y++)
	{
		auto row = &mapMonsterIds[y * width];
		for (size_t x = 0; x < width; x++)
		{
			auto monsterId = row[x];
			auto monster = ObjectGenerator::NewObject(monsterId);
			if (monster)
			{
				monster->Position() = { x * GameConfig::CELL_SIZE, y * GameConfig::CELL_SIZE };
				scene->AddObject(monster);
			}
		}
	}
}

//void AddDynamicObjects(Scene2D* scene)
//{
//	AddPlayer(scene);
//	AddMapRenderer(scene);
//}