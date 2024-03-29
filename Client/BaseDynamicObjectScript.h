#pragma once

#include "Components2D/Script/Script2D.h"

using namespace soft;

class BaseDynamicObjectScript : Traceable<BaseDynamicObjectScript>, public Script2D
{
public:
	struct DynamicObjectProperties
	{
		float hp = 100;
	};

protected:
	TRACEABLE_FRIEND();
	void Trace(Tracer* tracer)
	{
		using Base = Script2D;
		Base::Trace(tracer);
	}

public:
	DynamicObjectProperties m_dynamicObjectProperties;

protected:
	virtual void OnTakeDamage(GameObject2D* from, float many) {};
	virtual void OnDestroyed(GameObject2D* by) {};

	virtual bool IsRemovable()
	{
		return true;
	}

	virtual bool CanTakeDamage(GameObject2D* from, float many)
	{
		return true;
	}

public:
	inline bool TakeDamage(GameObject2D* from, float many)
	{
		if (DynamicObjectProperties().hp == 0.0f || !CanTakeDamage(from, many))
		{
			return false;
		}

		many = std::clamp(many, 0.0f, DynamicObjectProperties().hp);
		DynamicObjectProperties().hp -= many;
		OnTakeDamage(from, many);

		if (DynamicObjectProperties().hp == 0)
		{
			OnDestroyed(from);
			if (IsRemovable())
			{
				GetScene()->RemoveObject(GetObject());
			}
		}

		return true;
	}

	inline DynamicObjectProperties& DynamicObjectProperties()
	{
		return m_dynamicObjectProperties;
	}
};