#include "MathUtils.h"
#include "Utilities.h"
#include "nlohmann/json.hpp"
#include <Windows.h>
#include <fstream>
#include <iostream>
#include <unordered_map>
//#define DEBUG

#ifdef DEBUG
#define _DEBUGMESSAGE(fmt, ...) _MESSAGE(fmt __VA_OPT__(, ) __VA_ARGS__)
#else
#define _DEBUGMESSAGE(fmt, ...)
#endif

using std::unordered_map;
using std::vector;

struct PartData
{
	std::string partName = "";
	BGSMaterialType* material = nullptr;
	SpellItem* spell_victim = nullptr;
	SpellItem* spell_attacker = nullptr;
	SpellItem* spell_blocked_victim = nullptr;
	SpellItem* spell_blocked_attacker = nullptr;
	TESImageSpaceModifier* imod = nullptr;
	float damageThreshold = 0.f;
	float shakeDuration = 0.f;
	float shakeStrength = 0.f;
};

struct OMODData
{
	vector<PartData> parts;
	BGSMod::Attachment::Mod* mainOMOD = nullptr;
	BGSMod::Attachment::Mod* groundOMOD = nullptr;
};

struct ShieldData
{
	vector<OMODData> omods;
	bool isWeapon = false;
};

ptrdiff_t ProcessProjectileFX_PatchOffset = 0x1A1;
REL::Relocation<uintptr_t> ProcessProjectileFX{ REL::ID(806412), ProcessProjectileFX_PatchOffset };
REL::Relocation<uintptr_t> ptr_DoHitMe{ REL::ID(1546751), 0x921 };
REL::Relocation<uintptr_t> ptr_UpdateSceneGraph{ REL::ID(1318162), 0xD5 };
REL::Relocation<uintptr_t> ptr_Demand3D{ REL::ID(736753), 0xB4 };
static uintptr_t DoHitMeOrig;
static uintptr_t UpdateSceneGraphOrig;
static uintptr_t Demand3DOrig;
static unordered_map<uint32_t, ShieldData> shieldDataMap;
static unordered_map<uint32_t, OMODData> omodDataCache;	 //Used for weapons only
static unordered_map<std::string, char> shieldPartsCache;
static ActorValueInfo* damageThresholdAdd;
static ActorValueInfo* damageThresholdMul;
static BGSProjectile* colCheckProj;
static PlayerCharacter* pc;
static PlayerCamera* pcam;

bool isInWorkbench = false;

const static unordered_map<std::string, char> animatedBones = {
	{ "Pelvis", 0 },
	{ "LLeg_Thigh", 0 },
	{ "LLeg_Calf", 0 },
	{ "LLeg_Foot", 0 },
	{ "RLeg_Thigh", 0 },
	{ "RLeg_Calf", 0 },
	{ "RLeg_Foot", 0 },
	{ "Spine1", 0 },
	{ "Spine2", 0 },
	{ "Chest", 0 },
	{ "LArm_Collarbone", 0 },
	{ "LArm_UpperArm", 0 },
	{ "LArm_ForeArm1", 0 },
	{ "LArm_ForeArm2", 0 },
	{ "LArm_ForeArm3", 0 },
	{ "LArm_Hand", 0 },
	{ "RArm_Collarbone", 0 },
	{ "RArm_UpperArm", 0 },
	{ "RArm_ForeArm1", 0 },
	{ "RArm_ForeArm2", 0 },
	{ "RArm_ForeArm3", 0 },
	{ "PipboyBone", 0 },
	{ "RArm_Hand", 0 },
	{ "WeaponLeft", 0 },
	{ "Weapon", 0 },
	{ "WeaponBolt", 0 },
	{ "WeaponExtra1", 0 },
	{ "WeaponExtra2", 0 },
	{ "WeaponExtra3", 0 },
	{ "WeaponMagazine", 0 },
	{ "WeaponMagazineChild1", 0 },
	{ "WeaponMagazineChild2", 0 },
	{ "WeaponMagazineChild3", 0 },
	{ "WeaponMagazineChild4", 0 },
	{ "WeaponMagazineChild5", 0 },
	{ "WeaponOptics1", 0 },
	{ "WeaponOptics2", 0 },
	{ "WeaponTrigger", 0 },
	{ "LArm_UpperTwist1", 0 },
	{ "LArm_UpperTwist2", 0 },
	{ "RArm_UpperTwist1", 0 },
	{ "RArm_UpperTwist2", 0 },
	{ "LLeg_Toe1", 0 },
	{ "RLeg_Toe1", 0 },
	{ "LArm_Finger11", 0 },
	{ "LArm_Finger12", 0 },
	{ "LArm_Finger13", 0 },
	{ "LArm_Finger21", 0 },
	{ "LArm_Finger22", 0 },
	{ "LArm_Finger23", 0 },
	{ "LArm_Finger31", 0 },
	{ "LArm_Finger32", 0 },
	{ "LArm_Finger33", 0 },
	{ "LArm_Finger41", 0 },
	{ "LArm_Finger42", 0 },
	{ "LArm_Finger43", 0 },
	{ "LArm_Finger51", 0 },
	{ "LArm_Finger52", 0 },
	{ "LArm_Finger53", 0 },
	{ "RArm_Finger11", 0 },
	{ "RArm_Finger12", 0 },
	{ "RArm_Finger13", 0 },
	{ "RArm_Finger21", 0 },
	{ "RArm_Finger22", 0 },
	{ "RArm_Finger23", 0 },
	{ "RArm_Finger31", 0 },
	{ "RArm_Finger32", 0 },
	{ "RArm_Finger33", 0 },
	{ "RArm_Finger41", 0 },
	{ "RArm_Finger42", 0 },
	{ "RArm_Finger43", 0 },
	{ "RArm_Finger51", 0 },
	{ "RArm_Finger52", 0 },
	{ "RArm_Finger53", 0 }
};

bool HasMod(BGSInventoryItem* invitem, BGSMod::Attachment::Mod* mod)
{
	if (invitem->stackData->extra) {
		BGSObjectInstanceExtra* extraData = (BGSObjectInstanceExtra*)invitem->stackData->extra->GetByType(EXTRA_DATA_TYPE::kObjectInstance);
		if (extraData) {
			auto data = extraData->values;
			if (data && data->buffer) {
				uintptr_t buf = (uintptr_t)(data->buffer);
				for (uint32_t i = 0; i < data->size / 0x8; i++) {
					BGSMod::Attachment::Mod* omod = (BGSMod::Attachment::Mod*)TESForm::GetFormByID(*(uint32_t*)(buf + i * 0x8));
					if (omod == mod) {
						return true;
					}
				}
			}
		}
	}
	return false;
}

unordered_map<uint32_t, ShieldData>::iterator GetShieldData(uint32_t formID)
{
	auto sdlookup = shieldDataMap.find(formID);
	_DEBUGMESSAGE("GetShieldData - Searching formID %llx", formID);
	if (sdlookup != shieldDataMap.end()) {
		_DEBUGMESSAGE("GetShieldData - Found");
		return sdlookup;
	}
	return shieldDataMap.end();
}

std::vector<unordered_map<uint32_t, ShieldData>::iterator> GetEquippedShieldDataList(Actor* a)
{
	std::vector<unordered_map<uint32_t, ShieldData>::iterator> shieldList;
	if (!a || !a->inventoryList)
		return shieldList;

	_DEBUGMESSAGE("GetEquippedShieldDataList - Checking Actor formID %llx", a->formID);
	for (auto invitem = a->inventoryList->data.begin(); invitem != a->inventoryList->data.end(); ++invitem) {
		if (invitem->stackData->IsEquipped()) {
			if (invitem->object->formType == ENUM_FORM_ID::kWEAP) {
				auto sdlookup = GetShieldData(invitem->object->formID);
				if (sdlookup != shieldDataMap.end()) {
					for (auto omodit = sdlookup->second.omods.begin(); omodit != sdlookup->second.omods.end(); ++omodit) {
						if (HasMod(invitem, omodit->mainOMOD)) {
							shieldList.push_back(sdlookup);
							_DEBUGMESSAGE("GetEquippedShieldDataList - Equipped weapon %llx with omod %llx found", invitem->object->formID, omodit->mainOMOD->formID);
						}
					}
				}
			} else if (invitem->object->formType == ENUM_FORM_ID::kARMO) {
				auto sdlookup = GetShieldData(invitem->object->formID);
				if (sdlookup != shieldDataMap.end()) {
					shieldList.push_back(sdlookup);
					_DEBUGMESSAGE("GetEquippedShieldDataList - Equipped armor %llx found", invitem->object->formID);
				}
			}
		}
	}
	return shieldList;
}

bool IsShield(uint32_t formID)
{
	if (GetShieldData(formID) != shieldDataMap.end()) {
		return true;
	}
	return false;
}

void CacheOMODData(Actor* a)
{
	if (!a->inventoryList) {
		return;
	}
	_DEBUGMESSAGE("CacheOMODData - Checking Actor formID %llx", a->formID);
	for (auto invitem = a->inventoryList->data.begin(); invitem != a->inventoryList->data.end(); ++invitem) {
		if (invitem->stackData->IsEquipped() && invitem->object->formType == ENUM_FORM_ID::kWEAP) {
			auto sdlookup = GetShieldData(invitem->object->formID);
			if (sdlookup != shieldDataMap.end()) {
				for (auto omodit = sdlookup->second.omods.begin(); omodit != sdlookup->second.omods.end(); ++omodit) {
					if (HasMod(invitem, omodit->mainOMOD)) {
						omodDataCache.insert(std::pair<uint32_t, OMODData>(a->formID, *omodit));
						_DEBUGMESSAGE("CacheOMODData - Cached weapon %llx with omod %llx", invitem->object->formID, omodit->mainOMOD->formID);
						return;
					}
				}
			}
		}
	}
	_DEBUGMESSAGE("CacheOMODData - No weapon with shield found");
	omodDataCache.erase(a->formID);
}

OMODData* GetCurrentOMODData(Actor* a)
{
	auto odlookup = omodDataCache.find(a->formID);
	if (odlookup != omodDataCache.end()) {
		return &odlookup->second;
	} else {
		CacheOMODData(a);
		odlookup = omodDataCache.find(a->formID);
		if (odlookup != omodDataCache.end()) {
			return &odlookup->second;
		}
	}
	return nullptr;
}

void HookedDoHitMe(Actor* a, HitData& hitData)
{
	bool doDamage = true;
	bool hasCollObj = false;
	std::vector<PartData>::iterator blockedPart;
	auto sdlist = GetEquippedShieldDataList(a);
	for (auto sdlookup = sdlist.begin(); sdlookup != sdlist.end(); ++sdlookup) {
#ifdef DEBUG
		if (sdlookup == sdlist.begin()) {
			_DEBUGMESSAGE("HookedDoHitMe - ShieldHolder %llx hit with dmg %f", a->formID, hitData.totalDamage);
			if (hitData.impactData.collisionObj && hitData.impactData.collisionObj->sceneObject) {
				_DEBUGMESSAGE("HookedDoHitMe - Collision object %s at %llx", hitData.impactData.collisionObj->sceneObject->name.c_str(), hitData.impactData.collisionObj->sceneObject);
			}
			if (hitData.sourceHandle.get().get()) {
				_DEBUGMESSAGE("HookedDoHitMe - Source formID %llx at %llx", hitData.sourceHandle.get()->formID, hitData.sourceHandle.get().get());
			}
		}
#endif
		OMODData* od = nullptr;
		if ((*sdlookup)->second.isWeapon) {
			od = GetCurrentOMODData(a);
		} else {
			od = &(*sdlookup)->second.omods.at(0);
		}
		//_MESSAGE("Actor %llx hit with dmg %f from %llx", a, hitData.totalDamage, hitData.source.object);
		if (!od) {
			_MESSAGE("HookedDoHitMe - Actor formID %llx Couldn't retrieve OMODData!", a->formID);
		} else {
			float dtAdd = a->GetActorValue(*damageThresholdAdd);
			float dtMul = a->GetActorValue(*damageThresholdMul);
			if (hitData.impactData.collisionObj) {
				_DEBUGMESSAGE("HookedDoHitMe - Projectile Check");
				for (auto partit = od->parts.begin(); partit != od->parts.end(); ++partit) {
					NiAVObject* parent = hitData.impactData.collisionObj->sceneObject;
					if (parent && parent->name == partit->partName) {
						_DEBUGMESSAGE("HookedDoHitMe - Threshold %f", (partit->damageThreshold + dtAdd) * dtMul);
						hasCollObj = true;
						blockedPart = partit;
						if (partit->damageThreshold < 0 || hitData.totalDamage < (partit->damageThreshold + dtAdd) * dtMul) {
							doDamage = false;
							hitData.SetAllDamageToZero();
							_DEBUGMESSAGE("HookedDoHitMe - Damage blocked");
							break;
						}
					}
				}
			} else {
				if (hitData.attackData) {
					_DEBUGMESSAGE("HookedDoHitMe - Melee Check");
					TESObjectREFR* attacker = hitData.attackerHandle.get().get();
					if (attacker && attacker->formType == ENUM_FORM_ID::kACHR) {
						NiPoint3 eyeAttacker, dirAttacker, attackerCenter;
						((ActorEx*)attacker)->GetEyeVector(eyeAttacker, dirAttacker, false);
						((TESObjectREFREx*)attacker)->GetObjectCenter(attackerCenter);
						F4::bhkPickData pick = F4::bhkPickData();
						dirAttacker.z = 0;
						dirAttacker = Normalize(dirAttacker);
						GetPickData(attackerCenter, attackerCenter + dirAttacker * 1000.f, (Actor*)attacker, colCheckProj, pick);
						if (pick.HasHit()) {
							NiPoint3 pickPos = NiPoint3(*(float*)((uintptr_t)&pick + 0x60), *(float*)((uintptr_t)&pick + 0x64), *(float*)((uintptr_t)&pick + 0x68)) / *ptr_fBS2HkScale;
							NiAVObject* closestBone = ((ActorEx*)a)->GetClosestBone(pickPos, dirAttacker);
							_DEBUGMESSAGE("HookedDoHitMe - Closest Bone %llx (%s)", closestBone, closestBone->name.c_str());
							if (closestBone) {
								for (auto partit = od->parts.begin(); partit != od->parts.end(); ++partit) {
									if (closestBone->name == partit->partName) {
										_DEBUGMESSAGE("HookedDoHitMe - Threshold %f", (partit->damageThreshold + dtAdd) * dtMul);
										hasCollObj = true;
										blockedPart = partit;
										if (partit->damageThreshold < 0 || hitData.totalDamage < (partit->damageThreshold + dtAdd) * dtMul) {
											doDamage = false;
											hitData.SetAllDamageToZero();
											_DEBUGMESSAGE("HookedDoHitMe - Damage blocked");
											break;
										}
									}
								}
							}
						}
					}
				} else {
					TESObjectREFR* source = hitData.sourceHandle.get().get();
					if (source) {
						_DEBUGMESSAGE("HookedDoHitMe - Explosion Check");
						BGSProjectile* baseProj = hitData.ammo ? hitData.ammo->data.projectile : nullptr;
						TESObjectWEAP* weap = (TESObjectWEAP*)hitData.source.object;
						TESObjectWEAP::InstanceData* weapInstance = (TESObjectWEAP::InstanceData*)hitData.source.instanceData.get();
						if (weapInstance) {
							if (weapInstance->rangedData && weapInstance->rangedData->overrideProjectile) {
								baseProj = weapInstance->rangedData->overrideProjectile;
							} else if (weapInstance->ammo && weapInstance->ammo->data.projectile) {
								baseProj = weapInstance->ammo->data.projectile;
							}
						} else if (weap) {
							if (weap->weaponData.rangedData && weap->weaponData.rangedData->overrideProjectile) {
								baseProj = weap->weaponData.rangedData->overrideProjectile;
							} else if (weap->weaponData.ammo && weap->weaponData.ammo->data.projectile) {
								baseProj = weap->weaponData.ammo->data.projectile;
							}
						}
						if ((baseProj && baseProj->data.explosionType) || source->GetObjectReference()->formType == ENUM_FORM_ID::kEXPL) {
							NiPoint3 actorCenter;
							((TESObjectREFREx*)a)->GetObjectCenter(actorCenter);
							F4::bhkPickData pick = F4::bhkPickData();
							NiPoint3 expDir = Normalize(NiPoint3(actorCenter - source->data.location));
							GetPickData(source->data.location, source->data.location + expDir * 5000.f, a, colCheckProj, pick, false);
							if (pick.HasHit()) {
								NiPoint3 pickPos = NiPoint3(*(float*)((uintptr_t)&pick + 0x60), *(float*)((uintptr_t)&pick + 0x64), *(float*)((uintptr_t)&pick + 0x68)) / *ptr_fBS2HkScale;
								NiAVObject* closestBone = ((ActorEx*)a)->GetClosestBone(pickPos, expDir);
								_DEBUGMESSAGE("HookedDoHitMe - Closest Bone %llx (%s)", closestBone, closestBone->name.c_str());
								if (closestBone) {
									for (auto partit = od->parts.begin(); partit != od->parts.end(); ++partit) {
										if (closestBone->name == partit->partName) {
											_DEBUGMESSAGE("HookedDoHitMe - Threshold %f", (partit->damageThreshold + dtAdd) * dtMul);
											hasCollObj = true;
											blockedPart = partit;
											if (partit->damageThreshold < 0 || hitData.totalDamage < (partit->damageThreshold + dtAdd) * dtMul) {
												doDamage = false;
												hitData.SetAllDamageToZero();
												_DEBUGMESSAGE("HookedDoHitMe - Damage blocked");
												break;
											}
										}
									}
								}
							}
						}
					}
				}
			}
		}
	}
	if (hasCollObj) {
		TESObjectREFR* attacker = hitData.attackerHandle.get().get();
		if (doDamage) {
			if (blockedPart->spell_victim) {
				_DEBUGMESSAGE("HookedDoHitMe - Casting spell on victim (Non-block)");
				blockedPart->spell_victim->Cast(a, a, a, GameVM::GetSingleton()->GetVM().get());
			}
			if (blockedPart->spell_attacker) {
				if (attacker && attacker->formType == ENUM_FORM_ID::kACHR) {
					_DEBUGMESSAGE("HookedDoHitMe - Casting spell on attacker (Non-block)");
					blockedPart->spell_attacker->Cast(a, attacker, (Actor*)attacker, GameVM::GetSingleton()->GetVM().get());
				}
			}
		} else {
			if (blockedPart->spell_blocked_victim) {
				_DEBUGMESSAGE("HookedDoHitMe - Casting spell on victim (Blocked)");
				blockedPart->spell_blocked_victim->Cast(a, a, a, GameVM::GetSingleton()->GetVM().get());
			}
			if (blockedPart->spell_blocked_attacker) {
				if (attacker && attacker->formType == ENUM_FORM_ID::kACHR) {
					_DEBUGMESSAGE("HookedDoHitMe - Casting spell on attacker (Blocked)");
					blockedPart->spell_blocked_attacker->Cast(a, attacker, (Actor*)attacker, GameVM::GetSingleton()->GetVM().get());
				}
			}
		}
	}
	if (doDamage) {
		typedef bool (*FnDoHitMe)(Actor*, const HitData&);
		FnDoHitMe fn = (FnDoHitMe)DoHitMeOrig;
		if (fn)
			fn(a, hitData);
	}
}

void HookedUpdateSceneGraph(PlayerCharacter* p)
{
	if (p->Get3D(true) == p->Get3D()) {
		NiAVObject* fpNode = p->Get3D(true);
		NiAVObject* tpNode = p->Get3D(false);
#ifdef DEBUG
		tpNode->SetAppCulled(false);
#endif
		NiNode* camera = (NiNode*)fpNode->GetObjectByName("Camera");
		Visit(tpNode, [&](NiAVObject* obj) {
			if (obj->name.length() != 0) {
				NiAVObject* found = fpNode->GetObjectByName(obj->name);
				if (found) {
					if (obj->name == "Chest") {
						NiPoint3 fpdiff = found->world.translate - camera->world.translate;
						obj->local.rotate = found->world.rotate * Transpose(obj->parent->world.rotate);
						obj->local.translate = obj->parent->world.rotate * (*F4::ptr_kCurrentWorldLoc + fpdiff - obj->parent->world.translate);
						NiUpdateData ud = NiUpdateData();
						ud.unk10 = 0x303;
						obj->UpdateTransforms(ud);
					} else if (animatedBones.find(std::string(obj->name)) != animatedBones.end()) {
						if (obj->parent && found->parent) {
							obj->local.rotate = found->world.rotate * Transpose(obj->parent->world.rotate);
							obj->local.translate = obj->parent->world.rotate * (found->world.translate - found->parent->world.translate);
						} else {
							obj->local.rotate = found->local.rotate;
							obj->local.translate = found->local.translate;
						}
						NiUpdateData ud = NiUpdateData();
						ud.unk10 = 0x303;
						obj->UpdateTransforms(ud);
					}
				}
			}
			return false;
		});
	}
	typedef bool (*FnUpdateSceneGraph)(PlayerCharacter*);
	FnUpdateSceneGraph fn = (FnUpdateSceneGraph)UpdateSceneGraphOrig;
	if (fn)
		fn(p);
}

NiAVObject* HookedOMODDemand3D(BGSMod::Attachment::Mod* mod, uintptr_t loadData, bool isFP, bool highres)
{
	if (mod->targetFormType == ENUM_FORM_ID::kARMO) {
		_DEBUGMESSAGE("HookedOMODDemand3D - Armor OMOD formID %llx is requesting 3D", mod->formID);
		TESObjectREFR* ref = *(TESObjectREFR**)(loadData + 0x330);
		if (ref && ref->data.objectReference) {
			_DEBUGMESSAGE("HookedOMODDemand3D - Ref formID %llx, base formID %llx", ref->formID, ref->data.objectReference->formID);
			if (IsShield(ref->data.objectReference->formID)) {
				_DEBUGMESSAGE("HookedOMODDemand3D - Returning nullptr for shield");
				return nullptr;
			}
		}
	}
	typedef NiAVObject* (*FnDemand3D)(BGSMod::Attachment::Mod*, uintptr_t, bool, bool);
	FnDemand3D fn = (FnDemand3D)Demand3DOrig;
	NiAVObject* ret;
	if (fn) {
		ret = fn(mod, loadData, isFP, highres);
		return ret;
	}

	return nullptr;
}

void SetMainOMODLooseMod(const OMODData& od)
{
	TESObjectMISC* looseMod = ((BGSMod::Attachment::ModEx*)od.groundOMOD)->GetLooseMod();
	if (looseMod) {
		((BGSMod::Attachment::ModEx*)od.mainOMOD)->SetLooseMod(looseMod);
		((BGSMod::Attachment::ModEx*)od.groundOMOD)->SetLooseMod(nullptr);
		_MESSAGE("Loose mod swapped ground->main");
	}
}

void SetGroundOMODLooseMod(const OMODData& od)
{
	TESObjectMISC* looseMod = ((BGSMod::Attachment::ModEx*)od.mainOMOD)->GetLooseMod();
	if (looseMod) {
		((BGSMod::Attachment::ModEx*)od.groundOMOD)->SetLooseMod(looseMod);
		((BGSMod::Attachment::ModEx*)od.mainOMOD)->SetLooseMod(nullptr);
		_MESSAGE("Loose mod swapped main->ground");
	}
}

void AttachMainOMOD_Internal(Actor* a, const OMODData& od, BGSInventoryItem* invitem)
{
	if (invitem->stackData->extra) {
		BGSObjectInstanceExtra* extraData = (BGSObjectInstanceExtra*)invitem->stackData->extra->GetByType(EXTRA_DATA_TYPE::kObjectInstance);
		if (extraData) {
			extraData->RemoveMod(od.groundOMOD, 0xFFu);
			extraData->AddMod(*od.mainOMOD, 0, 1, true);
		}
	}
}

void AttachGroundOMOD_Internal(Actor* a, const OMODData& od, BGSInventoryItem* invitem)
{
	if (invitem->stackData->extra) {
		BGSObjectInstanceExtra* extraData = (BGSObjectInstanceExtra*)invitem->stackData->extra->GetByType(EXTRA_DATA_TYPE::kObjectInstance);
		if (extraData) {
			extraData->RemoveMod(od.mainOMOD, 0xFFu);
			extraData->AddMod(*od.groundOMOD, 0, 1, true);
		}
	}
}

void AttachMainOMOD(Actor* a, uint32_t formId, const ShieldData& sd)
{
	if (!a->inventoryList) {
		return;
	}
	_DEBUGMESSAGE("AttachMainOMOD - Actor %llx formID %llx", a->formID, formId);
	for (auto invitem = a->inventoryList->data.begin(); invitem != a->inventoryList->data.end(); ++invitem) {
		if (invitem->object->formType == ENUM_FORM_ID::kWEAP) {
			if (invitem->object->formID == formId) {
				_DEBUGMESSAGE("AttachMainOMOD - Found item in inventory");
				if (invitem->stackData->IsEquipped()) {
					_DEBUGMESSAGE("AttachMainOMOD - Is Equipped");
					for (auto omodit = sd.omods.begin(); omodit != sd.omods.end(); ++omodit) {
						if (HasMod(invitem, omodit->groundOMOD)) {
							AttachMainOMOD_Internal(a, *omodit, invitem);
							if (a == pc) {
								SetMainOMODLooseMod(*omodit);
							}
							omodDataCache.insert(std::pair<uint32_t, OMODData>(a->formID, *omodit));
							_MESSAGE("Actor %llx Equip->AttachMainOMOD", a->formID);
							return;
						}
					}
				}
			}
		}
	}
}

void AttachGroundOMOD(Actor* a, uint32_t formId, const ShieldData& sd)
{
	if (!a->inventoryList) {
		return;
	}
	_DEBUGMESSAGE("AttachGroundOMOD - Actor %llx formID %llx", a->formID, formId);
	for (auto invitem = a->inventoryList->data.begin(); invitem != a->inventoryList->data.end(); ++invitem) {
		if (invitem->object->formType == ENUM_FORM_ID::kWEAP) {
			if (invitem->object->formID == formId) {
				_DEBUGMESSAGE("AttachGroundOMOD - Found item in inventory");
				for (auto omodit = sd.omods.begin(); omodit != sd.omods.end(); ++omodit) {
					if (HasMod(invitem, omodit->mainOMOD)) {
						AttachGroundOMOD_Internal(a, *omodit, invitem);
						if (a == pc) {
							SetGroundOMODLooseMod(*omodit);
						}
						omodDataCache.erase(a->formID);
						_MESSAGE("Actor %llx Unequip->AttachGroundOMOD", a->formID);
						return;
					}
				}
			}
		}
	}
}

void ActivateShieldCollisionObjects(Actor* a)
{
	if (!a || !a->Get3D(false)) {
		_DEBUGMESSAGE("ActivateShieldCollisionObjects - Actor does not exist!");
		return;
	}
	if (!a->parentCell) {
		_DEBUGMESSAGE("ActivateShieldCollisionObjects - Actor formID %llx does not have a parent cell!", a->formID);
		return;
	}
	NiNode* node = (NiNode*)a->Get3D(false)->IsNode();
	bhkWorld* world = a->parentCell->GetbhkWorld();
	if (node && node->children[0].get()) {
		Visit(node->children[0].get(), [&](NiAVObject* obj) {
			if (obj->name.length() != 0 && obj->collisionObject) {
				if (shieldPartsCache.find(obj->name.c_str()) != shieldPartsCache.end()) {
					_DEBUGMESSAGE("ActivateShieldCollisionObjects - Part %s is in database. Activating collision", obj->name.c_str());
					Visit(obj, [a, world](NiAVObject* obj) {
						if (!obj->collisionObject)
							return false;

						_DEBUGMESSAGE("Collision object found at obj %llx", obj);
						bhkNPCollisionObject* colObj = obj->collisionObject->IsbhkNPCollisionObject();
						if (!colObj || !world) {
							_DEBUGMESSAGE("Obj is not bhkNPCollisionObject or bhkWorld does not exist");
							return false;
						}
						hknpBSWorld* hkWorld = *(hknpBSWorld**)((uintptr_t)world + 0x60);
						if (hkWorld) {
							hkWorld->MarkForWrite();
						}
						colObj->CreateInstance(*world);
						CFilter filter;
						filter.filter = ((((ActorEx*)a)->GetCollisionFilter().filter >> 16) << 16) | 0x1408;
						colObj->SetCollisionFilterInfo(filter);
						colObj->SetMotionType(hknpMotionPropertiesId::Preset::KEYFRAMED);
						if (hkWorld) {
							hkWorld->UnmarkForWrite();
						}
						return false;
					});
					_DEBUGMESSAGE("ActivateShieldCollisionObjects - Done", obj->name.c_str());
				}
			}
			return false;
		});
	} else {
		_MESSAGE("ActivateShieldCollisionObjects - Actor formID %llx does not have Root node", a->formID);
	}
}

void CheckShieldOMOD(Actor* a, bool forceRequip = false)
{
	if (!a->inventoryList) {
		omodDataCache.erase(a->formID);
		return;
	}
	bool hasShield = false;
	bool activateCollision = false;
	_DEBUGMESSAGE("CheckShieldOMOD - Actor %llx", a->formID);
	for (auto invitem = a->inventoryList->data.begin(); invitem != a->inventoryList->data.end(); ++invitem) {
		if (invitem->object->formType == ENUM_FORM_ID::kWEAP) {
			auto sdlookup = GetShieldData(invitem->object->formID);
			if (sdlookup != shieldDataMap.end()) {
				_DEBUGMESSAGE("CheckShieldOMOD - Weapon Shield %llx", invitem->object->formID);
				for (auto omodit = sdlookup->second.omods.begin(); omodit != sdlookup->second.omods.end(); ++omodit) {
					if (invitem->stackData->IsEquipped()) {
						_DEBUGMESSAGE("CheckShieldOMOD - Is Equipped");
						if (HasMod(invitem, omodit->groundOMOD)) {
							AttachMainOMOD_Internal(a, *omodit, invitem);
							GameScript::PostModifyInventoryItemMod(a, invitem->object, false);
							hasShield = true;
							_MESSAGE("Actor %llx CheckShieldOMOD->AttachMainOMOD", a->formID);
							break;
						} else if (HasMod(invitem, omodit->mainOMOD)) {
							if (!CheckPA(a) || forceRequip) {
								//GameScript::PostModifyInventoryItemMod(a, invitem->object, false);
								activateCollision = true;
								//_DEBUGMESSAGE("CheckShieldOMOD - Reequipping Weapon");
							}
							hasShield = true;
							_DEBUGMESSAGE("CheckShieldOMOD - Shield already has mainOMOD");
							break;
						}
					} else {
						_DEBUGMESSAGE("CheckShieldOMOD - Is NOT Equipped");
						if (HasMod(invitem, omodit->mainOMOD)) {
							AttachGroundOMOD_Internal(a, *omodit, invitem);
							_MESSAGE("Actor %llx CheckShieldOMOD->AttachGroundOMOD", a->formID);
							break;
						}
					}
				}
			}
		} else if (invitem->object->formType == ENUM_FORM_ID::kARMO) {
			auto sdlookup = GetShieldData(invitem->object->formID);
			if (sdlookup != shieldDataMap.end()) {
				_DEBUGMESSAGE("CheckShieldOMOD - Armor Shield %llx", invitem->object->formID);
				if (invitem->stackData->IsEquipped()) {
					_DEBUGMESSAGE("CheckShieldOMOD - Is Equipped");
					if (!CheckPA(a) || forceRequip) {
						//GameScript::PostModifyInventoryItemMod(a, invitem->object, false);
						activateCollision = true;
						//_DEBUGMESSAGE("CheckShieldOMOD - Reequipping Armor");
					}
				}
			}
		}
	}
	if (!hasShield) {
		omodDataCache.erase(a->formID);
	}
	if (activateCollision) {
		ActivateShieldCollisionObjects(a);
	}
}

class EquipWatcher : public BSTEventSink<TESEquipEvent>
{
public:
	virtual BSEventNotifyControl ProcessEvent(const TESEquipEvent& evn, BSTEventSource<TESEquipEvent>* a_source)
	{
		if (!isInWorkbench) {
			TESForm* item = TESForm::GetFormByID(evn.formId);
			if (item && (item->formType == ENUM_FORM_ID::kWEAP)) {
				auto sdlookup = GetShieldData(evn.formId);
				if (sdlookup != shieldDataMap.end()) {
					if (evn.isEquip) {
						AttachMainOMOD(evn.a, evn.formId, sdlookup->second);
					} else {
						AttachGroundOMOD(evn.a, evn.formId, sdlookup->second);
					}
				}
			}
		}
		return BSEventNotifyControl::kContinue;
	}
	F4_HEAP_REDEFINE_NEW(EquipEventSink);
};

class ObjectLoadWatcher : public BSTEventSink<TESObjectLoadedEvent>
{
public:
	virtual BSEventNotifyControl ProcessEvent(const TESObjectLoadedEvent& evn, BSTEventSource<TESObjectLoadedEvent>* a_source)
	{
		if (!evn.loaded) {
			omodDataCache.erase(evn.formId);
			return BSEventNotifyControl::kContinue;
		}
		TESForm* form = TESForm::GetFormByID(evn.formId);
		if (form) {
			if (form->formType == ENUM_FORM_ID::kACHR) {
				Actor* a = static_cast<Actor*>(form);
				CheckShieldOMOD(a);
			}
		}
		return BSEventNotifyControl::kContinue;
	}
	F4_HEAP_REDEFINE_NEW(ObjectLoadWatcher);
};

class MenuWatcher : public BSTEventSink<MenuOpenCloseEvent>
{
	virtual BSEventNotifyControl ProcessEvent(const MenuOpenCloseEvent& evn, BSTEventSource<MenuOpenCloseEvent>* src) override
	{
		if (!evn.opening && evn.menuName == BSFixedString("LoadingMenu")) {
			omodDataCache.clear();
			BSTArray<ActorHandle>* highActorHandles = (BSTArray<ActorHandle>*)(F4::ptr_processLists.address() + 0x40);
			if (highActorHandles->size() > 0) {
				for (auto it = highActorHandles->begin(); it != highActorHandles->end(); ++it) {
					Actor* a = it->get().get();
					if (a && a->Get3D())
						CheckShieldOMOD(a, true);
				}
			}
			CheckShieldOMOD(pc, true);
		} else if (evn.menuName == BSFixedString("ExamineMenu")) {
			if (pc->interactingState != INTERACTING_STATE::kNotInteracting) {
				if (evn.opening) {
					isInWorkbench = true;
					if (pc->inventoryList) {
						for (auto invitem = pc->inventoryList->data.begin(); invitem != pc->inventoryList->data.end(); ++invitem) {
							if (invitem->stackData->IsEquipped()) {
								if (invitem->object->formType == ENUM_FORM_ID::kWEAP) {
									auto sdlookup = GetShieldData(invitem->object->formID);
									if (sdlookup != shieldDataMap.end()) {
										OMODData* od = GetCurrentOMODData(pc);
										if (!od) {
											_MESSAGE("ExamineMenu - Actor formID %llx Shield form ID %llx Couldn't retrieve OMODData!", pc->formID, sdlookup->first);
										} else {
											if (invitem->stackData->IsEquipped()) {
												if (HasMod(invitem, od->mainOMOD)) {
													AttachGroundOMOD_Internal(pc, *od, invitem);
													SetGroundOMODLooseMod(*od);
												}
											}
										}
									}
								}
							}
						}
					}
					_MESSAGE("ExamineMenu open");
				} else {
					isInWorkbench = false;
					CheckShieldOMOD(pc);
					_MESSAGE("ExamineMenu close");
				}
			}
		}
		return BSEventNotifyControl::kContinue;
	}

public:
	F4_HEAP_REDEFINE_NEW(MenuWatcher);
};

//This will intercept ProcessImpacts() function in Projectile's vtable and check if the collidee is holding a shield.
class ProjectileHooks : public Projectile
{
public:
	typedef bool (ProjectileHooks::*FnProcessImpacts)();

	bool CheckShield()
	{
		for (auto it = this->impacts.begin(); it != this->impacts.end(); ++it) {
			if (it->processed || !it->collidee.get() || it->collidee.get()->GetFormType() != ENUM_FORM_ID::kACHR || !it->colObj.get())
				continue;

			Actor* a = (Actor*)it->collidee.get().get();
			auto sdlist = GetEquippedShieldDataList(a);
			for (auto sdlookup = sdlist.begin(); sdlookup != sdlist.end(); ++sdlookup) {
				OMODData* od = nullptr;
				if ((*sdlookup)->second.isWeapon) {
					od = GetCurrentOMODData(a);
				} else {
					od = &(*sdlookup)->second.omods.at(0);
				}
				if (!od) {
					_MESSAGE("HookedDoHitMe - Actor formID %llx Couldn't retrieve OMODData!", a->formID);
				} else {
					for (auto partit = od->parts.begin(); partit != od->parts.end(); ++partit) {
						NiAVObject* parent = it->colObj.get()->sceneObject;
						if (parent && parent->name == partit->partName) {
							if (partit->material) {
								it->materialType = partit->material;
							}
							if (a == pc && pcam->currentState != pcam->cameraStates[CameraState::kFree]) {
								if (partit->imod) {
									F4::ApplyImageSpaceModifier(partit->imod, 1.f, nullptr);
								}
								if (partit->shakeDuration > 0 && partit->shakeStrength > 0) {
									F4::ShakeCamera(1.f, it->location, partit->shakeDuration, partit->shakeStrength);
								}
							}
							it->damageLimb = (BGSBodyPartDefs::LIMB_ENUM)BGSBodyPartData::RightArm2;
						}
					}
				}
			}
		}
		FnProcessImpacts fn = fnHash.at(*(uintptr_t*)this);
		return fn ? (this->*fn)() : false;
	}

	static void HookProcessImpacts(uintptr_t addr, uintptr_t offset)
	{
		FnProcessImpacts fn = SafeWrite64Function(addr + offset, &ProjectileHooks::CheckShield);
		fnHash.insert(std::pair<uintptr_t, FnProcessImpacts>(addr, fn));
	}

protected:
	static unordered_map<uintptr_t, FnProcessImpacts> fnHash;
};
unordered_map<uintptr_t, ProjectileHooks::FnProcessImpacts> ProjectileHooks::fnHash;

void InitializeShieldData()
{
	namespace fs = std::filesystem;
	fs::path jsonPath = fs::current_path();
	jsonPath += "\\Data\\F4SE\\Plugins\\ShieldFramework\\ShieldData";
	std::stringstream stream;
	fs::directory_entry jsonEntry{ jsonPath };
	if (!jsonEntry.exists()) {
		_MESSAGE("Shield Data directory does not exist!");
		return;
	}
	for (auto& it : fs::directory_iterator(jsonEntry)) {
		if (it.path().extension().compare(".json") == 0) {
			stream << it.path().filename();
			_MESSAGE("Loading shield data %s", stream.str().c_str());
			stream.str(std::string());
			std::ifstream reader;
			reader.open(it.path());
			nlohmann::json j;
			reader >> j;

			for (auto shieldit = j.begin(); shieldit != j.end(); ++shieldit) {
				std::string shieldFormIDstr;
				std::string shieldPlugin = SplitString(shieldit.key(), "|", shieldFormIDstr);
				if (shieldFormIDstr.length() != 0) {
					_MESSAGE("Getting Form: Form ID %s from %s", shieldFormIDstr.c_str(), shieldPlugin.c_str());
					uint32_t shieldFormID = std::stoi(shieldFormIDstr, 0, 16);
					TESForm* shieldForm = GetFormFromMod(shieldPlugin, shieldFormID);
					if (shieldForm) {
						uint32_t formID = shieldForm->formID;
						_MESSAGE("Shield FormID %llx", formID);
						if (shieldForm->formType == ENUM_FORM_ID::kWEAP) {
							if (shieldDataMap.find(formID) == shieldDataMap.end()) {
								ShieldData shieldData;
								shieldData.isWeapon = true;
								auto omodlookup = (*shieldit).find("OMODs");
								if (omodlookup != (*shieldit).end()) {
									for (auto omodit = (*omodlookup).begin(); omodit != (*omodlookup).end(); ++omodit) {
										OMODData od;
										auto odlookup = (*omodit).find("MainOMOD");
										if (odlookup != (*omodit).end()) {
											std::string omodFormIDstr;
											std::string omodPlugin = SplitString(odlookup.value().get<std::string>(), "|", omodFormIDstr);
											if (omodFormIDstr.length() != 0) {
												TESForm* omodForm = GetFormFromMod(omodPlugin, std::stoi(omodFormIDstr, 0, 16));
												if (omodForm && omodForm->formType == ENUM_FORM_ID::kOMOD) {
													od.mainOMOD = (BGSMod::Attachment::Mod*)omodForm;
													_MESSAGE("Main OMOD FormID %llx", od.mainOMOD->formID);
												}
											} else {
												_MESSAGE("Main OMOD data invalid. Check JSON. Recevied %s", (omodPlugin + omodFormIDstr).c_str());
											}
										}
										odlookup = (*omodit).find("GroundOMOD");
										if (odlookup != (*omodit).end()) {
											std::string omodFormIDstr;
											std::string omodPlugin = SplitString(odlookup.value().get<std::string>(), "|", omodFormIDstr);
											if (omodFormIDstr.length() != 0) {
												TESForm* omodForm = GetFormFromMod(omodPlugin, std::stoi(omodFormIDstr, 0, 16));
												if (omodForm && omodForm->formType == ENUM_FORM_ID::kOMOD) {
													od.groundOMOD = (BGSMod::Attachment::Mod*)omodForm;
													_MESSAGE("Ground OMOD FormID %llx", od.groundOMOD->formID);
												}
											} else {
												_MESSAGE("Ground OMOD data invalid. Check JSON. Recevied %s", (omodPlugin + omodFormIDstr).c_str());
											}
										}
										auto partlookup = (*omodit).find("Parts");
										if (partlookup != (*omodit).end()) {
											for (auto partit = (*partlookup).begin(); partit != (*partlookup).end(); ++partit) {
												PartData pd;
												pd.partName = partit.key();
												if (shieldPartsCache.find(pd.partName) == shieldPartsCache.end()) {
													shieldPartsCache.insert(std::pair<std::string, char>(pd.partName, 0));
												}
												auto lookup = (*partit).find("MaterialType");
												if (lookup != (*partit).end()) {
													BGSMaterialType* result = GetMaterialTypeByName(lookup.value().get<std::string>());
													if (result) {
														pd.material = result;
														_MESSAGE("MaterialType %s found at %llx", lookup.value().get<std::string>().c_str(), result);
													} else {
														_MESSAGE("MaterialType %s not found", lookup.value().get<std::string>().c_str());
													}
												}
												lookup = (*partit).find("SpellShielder");
												if (lookup != (*partit).end()) {
													std::string spellFormIDstr;
													std::string spellPlugin = SplitString(lookup.value().get<std::string>(), "|", spellFormIDstr);
													if (spellFormIDstr.length() > 0) {
														TESForm* result = GetFormFromMod(spellPlugin, std::stoi(spellFormIDstr, 0, 16));
														if (result && result->formType == ENUM_FORM_ID::kSPEL) {
															pd.spell_victim = (SpellItem*)result;
															_MESSAGE("SpellShielder - Spell %s found at %llx", ((SpellItem*)result)->fullName.c_str(), result);
														} else {
															_MESSAGE("SpellShielder - Spell %s not found", lookup.value().get<std::string>().c_str());
														}
													}
												}
												lookup = (*partit).find("SpellAttacker");
												if (lookup != (*partit).end()) {
													std::string spellFormIDstr;
													std::string spellPlugin = SplitString(lookup.value().get<std::string>(), "|", spellFormIDstr);
													if (spellFormIDstr.length() > 0) {
														TESForm* result = GetFormFromMod(spellPlugin, std::stoi(spellFormIDstr, 0, 16));
														if (result && result->formType == ENUM_FORM_ID::kSPEL) {
															pd.spell_attacker = (SpellItem*)result;
															_MESSAGE("SpellAttacker - Spell %s found at %llx", ((SpellItem*)result)->fullName.c_str(), result);
														} else {
															_MESSAGE("SpellAttacker - Spell %s not found", lookup.value().get<std::string>().c_str());
														}
													}
												}
												lookup = (*partit).find("SpellShielderOnBlock");
												if (lookup != (*partit).end()) {
													std::string spellFormIDstr;
													std::string spellPlugin = SplitString(lookup.value().get<std::string>(), "|", spellFormIDstr);
													if (spellFormIDstr.length() > 0) {
														TESForm* result = GetFormFromMod(spellPlugin, std::stoi(spellFormIDstr, 0, 16));
														if (result && result->formType == ENUM_FORM_ID::kSPEL) {
															pd.spell_blocked_victim = (SpellItem*)result;
															_MESSAGE("SpellShielderOnBlock - Spell %s found at %llx", ((SpellItem*)result)->fullName.c_str(), result);
														} else {
															_MESSAGE("SpellShielderOnBlock - Spell %s not found", lookup.value().get<std::string>().c_str());
														}
													}
												}
												lookup = (*partit).find("SpellAttackerOnBlock");
												if (lookup != (*partit).end()) {
													std::string spellFormIDstr;
													std::string spellPlugin = SplitString(lookup.value().get<std::string>(), "|", spellFormIDstr);
													if (spellFormIDstr.length() > 0) {
														TESForm* result = GetFormFromMod(spellPlugin, std::stoi(spellFormIDstr, 0, 16));
														if (result && result->formType == ENUM_FORM_ID::kSPEL) {
															pd.spell_blocked_attacker = (SpellItem*)result;
															_MESSAGE("SpellAttackerOnBlock - Spell %s found at %llx", ((SpellItem*)result)->fullName.c_str(), result);
														} else {
															_MESSAGE("SpellAttackerOnBlock - Spell %s not found", lookup.value().get<std::string>().c_str());
														}
													}
												}
												lookup = (*partit).find("IMOD");
												if (lookup != (*partit).end()) {
													std::string imodFormIDstr;
													std::string imodPlugin = SplitString(lookup.value().get<std::string>(), "|", imodFormIDstr);
													if (imodFormIDstr.length() > 0) {
														TESForm* result = GetFormFromMod(imodPlugin, std::stoi(imodFormIDstr, 0, 16));
														if (result && result->formType == ENUM_FORM_ID::kIMAD) {
															pd.imod = (TESImageSpaceModifier*)result;
															_MESSAGE("IMOD %s found at %llx", ((TESImageSpaceModifier*)result)->formEditorID.c_str(), result);
														} else {
															_MESSAGE("IMOD %s not found", lookup.value().get<std::string>().c_str());
														}
													}
												}
												lookup = (*partit).find("DamageThreshold");
												if (lookup != (*partit).end()) {
													pd.damageThreshold = lookup.value().get<float>();
													_MESSAGE("DamageThreshold set to %f", pd.damageThreshold);
												}
												lookup = (*partit).find("ShakeDuration");
												if (lookup != (*partit).end()) {
													pd.shakeDuration = lookup.value().get<float>();
													_MESSAGE("ShakeDuration set to %f", pd.shakeDuration);
												}
												lookup = (*partit).find("ShakeStrength");
												if (lookup != (*partit).end()) {
													pd.shakeStrength = lookup.value().get<float>();
													_MESSAGE("ShakeStrength set to %f", pd.shakeStrength);
												}
												od.parts.push_back(pd);
											}
										}
										if (od.mainOMOD && od.groundOMOD) {
											shieldData.omods.push_back(od);
										} else {
											_MESSAGE("Main OMOD or Ground OMOD not set correctly.");
										}
									}
								}
								shieldDataMap.insert(std::pair<uint32_t, ShieldData>(formID, shieldData));
							}
						} else if (shieldForm->formType == ENUM_FORM_ID::kARMO) {
							if (shieldDataMap.find(formID) == shieldDataMap.end()) {
								ShieldData shieldData;
								OMODData od;
								auto partlookup = (*shieldit).find("Parts");
								if (partlookup != (*shieldit).end()) {
									for (auto partit = (*partlookup).begin(); partit != (*partlookup).end(); ++partit) {
										PartData pd;
										pd.partName = partit.key();
										if (shieldPartsCache.find(pd.partName) == shieldPartsCache.end()) {
											shieldPartsCache.insert(std::pair<std::string, char>(pd.partName, 0));
										}
										auto lookup = (*partit).find("MaterialType");
										if (lookup != (*partit).end()) {
											BGSMaterialType* result = GetMaterialTypeByName(lookup.value().get<std::string>());
											if (result) {
												pd.material = result;
												_MESSAGE("MaterialType %s found at %llx", lookup.value().get<std::string>().c_str(), result);
											} else {
												_MESSAGE("MaterialType %s not found", lookup.value().get<std::string>().c_str());
											}
										}
										lookup = (*partit).find("SpellShielder");
										if (lookup != (*partit).end()) {
											std::string spellFormIDstr;
											std::string spellPlugin = SplitString(lookup.value().get<std::string>(), "|", spellFormIDstr);
											if (spellFormIDstr.length() > 0) {
												TESForm* result = GetFormFromMod(spellPlugin, std::stoi(spellFormIDstr, 0, 16));
												if (result && result->formType == ENUM_FORM_ID::kSPEL) {
													pd.spell_victim = (SpellItem*)result;
													_MESSAGE("SpellShielder - Spell %s found at %llx", ((SpellItem*)result)->fullName.c_str(), result);
												} else {
													_MESSAGE("SpellShielder - Spell %s not found", lookup.value().get<std::string>().c_str());
												}
											}
										}
										lookup = (*partit).find("SpellAttacker");
										if (lookup != (*partit).end()) {
											std::string spellFormIDstr;
											std::string spellPlugin = SplitString(lookup.value().get<std::string>(), "|", spellFormIDstr);
											if (spellFormIDstr.length() > 0) {
												TESForm* result = GetFormFromMod(spellPlugin, std::stoi(spellFormIDstr, 0, 16));
												if (result && result->formType == ENUM_FORM_ID::kSPEL) {
													pd.spell_attacker = (SpellItem*)result;
													_MESSAGE("SpellAttacker - Spell %s found at %llx", ((SpellItem*)result)->fullName.c_str(), result);
												} else {
													_MESSAGE("SpellAttacker - Spell %s not found", lookup.value().get<std::string>().c_str());
												}
											}
										}
										lookup = (*partit).find("SpellShielderOnBlock");
										if (lookup != (*partit).end()) {
											std::string spellFormIDstr;
											std::string spellPlugin = SplitString(lookup.value().get<std::string>(), "|", spellFormIDstr);
											if (spellFormIDstr.length() > 0) {
												TESForm* result = GetFormFromMod(spellPlugin, std::stoi(spellFormIDstr, 0, 16));
												if (result && result->formType == ENUM_FORM_ID::kSPEL) {
													pd.spell_blocked_victim = (SpellItem*)result;
													_MESSAGE("SpellShielderOnBlock - Spell %s found at %llx", ((SpellItem*)result)->fullName.c_str(), result);
												} else {
													_MESSAGE("SpellShielderOnBlock - Spell %s not found", lookup.value().get<std::string>().c_str());
												}
											}
										}
										lookup = (*partit).find("SpellAttackerOnBlock");
										if (lookup != (*partit).end()) {
											std::string spellFormIDstr;
											std::string spellPlugin = SplitString(lookup.value().get<std::string>(), "|", spellFormIDstr);
											if (spellFormIDstr.length() > 0) {
												TESForm* result = GetFormFromMod(spellPlugin, std::stoi(spellFormIDstr, 0, 16));
												if (result && result->formType == ENUM_FORM_ID::kSPEL) {
													pd.spell_blocked_attacker = (SpellItem*)result;
													_MESSAGE("SpellAttackerOnBlock - Spell %s found at %llx", ((SpellItem*)result)->fullName.c_str(), result);
												} else {
													_MESSAGE("SpellAttackerOnBlock - Spell %s not found", lookup.value().get<std::string>().c_str());
												}
											}
										}
										lookup = (*partit).find("IMOD");
										if (lookup != (*partit).end()) {
											std::string imodFormIDstr;
											std::string imodPlugin = SplitString(lookup.value().get<std::string>(), "|", imodFormIDstr);
											if (imodFormIDstr.length() > 0) {
												TESForm* result = GetFormFromMod(imodPlugin, std::stoi(imodFormIDstr, 0, 16));
												if (result && result->formType == ENUM_FORM_ID::kIMAD) {
													pd.imod = (TESImageSpaceModifier*)result;
													_MESSAGE("IMOD %s found at %llx", ((TESImageSpaceModifier*)result)->formEditorID.c_str(), result);
												} else {
													_MESSAGE("IMOD %s not found", lookup.value().get<std::string>().c_str());
												}
											}
										}
										lookup = (*partit).find("DamageThreshold");
										if (lookup != (*partit).end()) {
											pd.damageThreshold = lookup.value().get<float>();
											_MESSAGE("DamageThreshold set to %f", pd.damageThreshold);
										}
										lookup = (*partit).find("ShakeDuration");
										if (lookup != (*partit).end()) {
											pd.shakeDuration = lookup.value().get<float>();
											_MESSAGE("ShakeDuration set to %f", pd.shakeDuration);
										}
										lookup = (*partit).find("ShakeStrength");
										if (lookup != (*partit).end()) {
											pd.shakeStrength = lookup.value().get<float>();
											_MESSAGE("ShakeStrength set to %f", pd.shakeStrength);
										}
										od.parts.push_back(pd);
									}
								}
								shieldData.omods.push_back(od);
								shieldDataMap.insert(std::pair<uint32_t, ShieldData>(formID, shieldData));
							} else {
								_MESSAGE("Invalid form type. Should be weapon or armor");
							}
						} else {
							_MESSAGE("Invalid form.");
						}
					} else {
						_MESSAGE("Shield data invalid. Check JSON. Recevied %s", (shieldPlugin + shieldFormIDstr).c_str());
					}
				}
			}
		}
	}
}

void InitializeImpactData()
{
	namespace fs = std::filesystem;
	fs::path jsonPath = fs::current_path();
	jsonPath += "\\Data\\F4SE\\Plugins\\ShieldFramework\\ImpactData";
	std::stringstream stream;
	fs::directory_entry jsonEntry{ jsonPath };
	if (!jsonEntry.exists()) {
		_MESSAGE("ImpactData directory does not exist!");
		return;
	}
	BGSImpactDataSet* bullet_normal = (BGSImpactDataSet*)TESForm::GetFormByID(0x004B37);
	BGSImpactDataSet* bullet_heavy = (BGSImpactDataSet*)TESForm::GetFormByID(0x05DDEE);
	BGSImpactDataSet* bullet_explosive = (BGSImpactDataSet*)TESForm::GetFormByID(0x21C79C);
	BGSImpactDataSet* bullet_noparallax = (BGSImpactDataSet*)TESForm::GetFormByID(0x22CC14);
	BGSImpactDataSet* bullet_incendiary = (BGSImpactDataSet*)TESForm::GetFormByID(0x1B5EE6);
	BGSImpactDataSet* bullet_cryo = (BGSImpactDataSet*)TESForm::GetFormByID(0x06F11F);
	BGSImpactDataSet* flamethrower = (BGSImpactDataSet*)TESForm::GetFormByID(0x0D76F5);
	for (auto& it : fs::directory_iterator(jsonEntry)) {
		if (it.path().extension().compare(".json") == 0) {
			stream << it.path().filename();
			_MESSAGE("Loading impact data %s", stream.str().c_str());
			stream.str(std::string());
			std::ifstream reader;
			reader.open(it.path());
			nlohmann::json j;
			reader >> j;

			for (auto typeit = j.begin(); typeit != j.end(); ++typeit) {
				if (typeit.key() == "Normal") {
					for (auto impactit = (*typeit).begin(); impactit != (*typeit).end(); ++impactit) {
						auto matlookup = impactit.value().find("MaterialType");
						if (matlookup == impactit.value().end())
							continue;
						auto ipctlookup = impactit.value().find("Impact");
						if (ipctlookup == impactit.value().end())
							continue;
						std::string matname = matlookup.value().get<std::string>();
						if (matname.length() == 0)
							continue;
						BGSMaterialType* mat = GetMaterialTypeByName(matlookup.value().get<std::string>());
						BGSImpactData* ipct = (BGSImpactData*)GetFormFromMod((*ipctlookup.value().find("Mod")).get<std::string>(),
							std::stoi((*ipctlookup.value().find("FormID")).get<std::string>(), 0, 16));
						if (mat && ipct) {
							bullet_normal->impactMap.insert(BSTTuple<BGSMaterialType*, BGSImpactData*>(mat, ipct));
						} else {
							_MESSAGE("Impact data for %s type failed! Mat : %s, ImpactData %llx", matname.c_str(), ipct);
						}
					}
				} else if (typeit.key() == "Heavy") {
					for (auto impactit = (*typeit).begin(); impactit != (*typeit).end(); ++impactit) {
						auto matlookup = impactit.value().find("MaterialType");
						if (matlookup == impactit.value().end())
							continue;
						auto ipctlookup = impactit.value().find("Impact");
						if (ipctlookup == impactit.value().end())
							continue;
						std::string matname = matlookup.value().get<std::string>();
						if (matname.length() == 0)
							continue;
						BGSMaterialType* mat = GetMaterialTypeByName(matlookup.value().get<std::string>());
						BGSImpactData* ipct = (BGSImpactData*)GetFormFromMod((*ipctlookup.value().find("Mod")).get<std::string>(),
							std::stoi((*ipctlookup.value().find("FormID")).get<std::string>(), 0, 16));
						if (mat && ipct) {
							bullet_heavy->impactMap.insert(BSTTuple<BGSMaterialType*, BGSImpactData*>(mat, ipct));
						} else {
							_MESSAGE("Impact data for %s type failed! Mat : %s, ImpactData %llx", matname.c_str(), ipct);
						}
					}
				} else if (typeit.key() == "Explosive") {
					for (auto impactit = (*typeit).begin(); impactit != (*typeit).end(); ++impactit) {
						auto matlookup = impactit.value().find("MaterialType");
						if (matlookup == impactit.value().end())
							continue;
						auto ipctlookup = impactit.value().find("Impact");
						if (ipctlookup == impactit.value().end())
							continue;
						std::string matname = matlookup.value().get<std::string>();
						if (matname.length() == 0)
							continue;
						BGSMaterialType* mat = GetMaterialTypeByName(matlookup.value().get<std::string>());
						BGSImpactData* ipct = (BGSImpactData*)GetFormFromMod((*ipctlookup.value().find("Mod")).get<std::string>(),
							std::stoi((*ipctlookup.value().find("FormID")).get<std::string>(), 0, 16));
						if (mat && ipct) {
							bullet_explosive->impactMap.insert(BSTTuple<BGSMaterialType*, BGSImpactData*>(mat, ipct));
						} else {
							_MESSAGE("Impact data for %s type failed! Mat : %s, ImpactData %llx", matname.c_str(), ipct);
						}
					}
				} else if (typeit.key() == "NoParallax") {
					for (auto impactit = (*typeit).begin(); impactit != (*typeit).end(); ++impactit) {
						auto matlookup = impactit.value().find("MaterialType");
						if (matlookup == impactit.value().end())
							continue;
						auto ipctlookup = impactit.value().find("Impact");
						if (ipctlookup == impactit.value().end())
							continue;
						std::string matname = matlookup.value().get<std::string>();
						if (matname.length() == 0)
							continue;
						BGSMaterialType* mat = GetMaterialTypeByName(matlookup.value().get<std::string>());
						BGSImpactData* ipct = (BGSImpactData*)GetFormFromMod((*ipctlookup.value().find("Mod")).get<std::string>(),
							std::stoi((*ipctlookup.value().find("FormID")).get<std::string>(), 0, 16));
						if (mat && ipct) {
							bullet_noparallax->impactMap.insert(BSTTuple<BGSMaterialType*, BGSImpactData*>(mat, ipct));
						} else {
							_MESSAGE("Impact data for %s type failed! Mat : %s, ImpactData %llx", matname.c_str(), ipct);
						}
					}
				} else if (typeit.key() == "Incendiary") {
					for (auto impactit = (*typeit).begin(); impactit != (*typeit).end(); ++impactit) {
						auto matlookup = impactit.value().find("MaterialType");
						if (matlookup == impactit.value().end())
							continue;
						auto ipctlookup = impactit.value().find("Impact");
						if (ipctlookup == impactit.value().end())
							continue;
						std::string matname = matlookup.value().get<std::string>();
						if (matname.length() == 0)
							continue;
						BGSMaterialType* mat = GetMaterialTypeByName(matlookup.value().get<std::string>());
						BGSImpactData* ipct = (BGSImpactData*)GetFormFromMod((*ipctlookup.value().find("Mod")).get<std::string>(),
							std::stoi((*ipctlookup.value().find("FormID")).get<std::string>(), 0, 16));
						if (mat && ipct) {
							bullet_incendiary->impactMap.insert(BSTTuple<BGSMaterialType*, BGSImpactData*>(mat, ipct));
						} else {
							_MESSAGE("Impact data for %s type failed! Mat : %s, ImpactData %llx", matname.c_str(), ipct);
						}
					}
				} else if (typeit.key() == "Cryo") {
					for (auto impactit = (*typeit).begin(); impactit != (*typeit).end(); ++impactit) {
						auto matlookup = impactit.value().find("MaterialType");
						if (matlookup == impactit.value().end())
							continue;
						auto ipctlookup = impactit.value().find("Impact");
						if (ipctlookup == impactit.value().end())
							continue;
						std::string matname = matlookup.value().get<std::string>();
						if (matname.length() == 0)
							continue;
						BGSMaterialType* mat = GetMaterialTypeByName(matlookup.value().get<std::string>());
						BGSImpactData* ipct = (BGSImpactData*)GetFormFromMod((*ipctlookup.value().find("Mod")).get<std::string>(),
							std::stoi((*ipctlookup.value().find("FormID")).get<std::string>(), 0, 16));
						if (mat && ipct) {
							bullet_cryo->impactMap.insert(BSTTuple<BGSMaterialType*, BGSImpactData*>(mat, ipct));
						} else {
							_MESSAGE("Impact data for %s type failed! Mat : %s, ImpactData %llx", matname.c_str(), ipct);
						}
					}
				} else if (typeit.key() == "FlameThrower") {
					for (auto impactit = (*typeit).begin(); impactit != (*typeit).end(); ++impactit) {
						auto matlookup = impactit.value().find("MaterialType");
						if (matlookup == impactit.value().end())
							continue;
						auto ipctlookup = impactit.value().find("Impact");
						if (ipctlookup == impactit.value().end())
							continue;
						std::string matname = matlookup.value().get<std::string>();
						if (matname.length() == 0)
							continue;
						BGSMaterialType* mat = GetMaterialTypeByName(matlookup.value().get<std::string>());
						BGSImpactData* ipct = (BGSImpactData*)GetFormFromMod((*ipctlookup.value().find("Mod")).get<std::string>(),
							std::stoi((*ipctlookup.value().find("FormID")).get<std::string>(), 0, 16));
						if (mat && ipct) {
							flamethrower->impactMap.insert(BSTTuple<BGSMaterialType*, BGSImpactData*>(mat, ipct));
						} else {
							_MESSAGE("Impact data for %s type failed! Mat : %s, ImpactData %llx", matname.c_str(), ipct);
						}
					}
				}
			}
		}
	}
}

void InitializeFramework()
{
	uint64_t addr;
	uint64_t offset = 0x680;
	addr = Projectile::VTABLE[0].address();
	_MESSAGE("Patching Projectile %llx", addr);
	ProjectileHooks::HookProcessImpacts(addr, offset);

	addr = MissileProjectile::VTABLE[0].address();
	_MESSAGE("Patching MissileProjectile %llx", addr);
	ProjectileHooks::HookProcessImpacts(addr, offset);

	addr = GrenadeProjectile::VTABLE[0].address();
	_MESSAGE("Patching GrenadeProjectile %llx", addr);
	ProjectileHooks::HookProcessImpacts(addr, offset);

	addr = BeamProjectile::VTABLE[0].address();
	_MESSAGE("Patching BeamProjectile %llx", addr);
	ProjectileHooks::HookProcessImpacts(addr, offset);

	addr = FlameProjectile::VTABLE[0].address();
	_MESSAGE("Patching FlameProjectile %llx", addr);
	ProjectileHooks::HookProcessImpacts(addr, offset);

	addr = ConeProjectile::VTABLE[0].address();
	_MESSAGE("Patching ConeProjectile %llx", addr);
	ProjectileHooks::HookProcessImpacts(addr, offset);

	addr = BarrierProjectile::VTABLE[0].address();
	_MESSAGE("Patching BarrierProjectile %llx", addr);
	ProjectileHooks::HookProcessImpacts(addr, offset);

	//Fallout4.exe+0xFD58A0 is the function that plays the impact effect based on various impact data.
	//0x1A1 from this function is the part where it checks the form type of collidee and play the impact effect based on the race data.
	//This will modify that behavior, forcing the game to always use the projectile impact data's materialType for the impact effect.
	_MESSAGE("Patching impact form type check %llx", ProcessProjectileFX.address());
	REL::safe_write(ProcessProjectileFX.address(), (uint8_t)0xEB);

	damageThresholdAdd = GetAVIFByEditorID(std::string("ShieldDTAdd"));
	damageThresholdMul = GetAVIFByEditorID(std::string("ShieldDTMul"));
	colCheckProj = (BGSProjectile*)GetFormFromMod("ShieldFramework.esm", 0x2682);

	pc = PlayerCharacter::GetSingleton();
	pcam = PlayerCamera::GetSingleton();
	_MESSAGE("PlayerCharacter %llx", pc);
}

extern "C" DLLEXPORT bool F4SEAPI F4SEPlugin_Query(const F4SE::QueryInterface* a_f4se, F4SE::PluginInfo* a_info)
{
#ifndef NDEBUG
	auto sink = std::make_shared<spdlog::sinks::msvc_sink_mt>();
#else
	auto path = logger::log_directory();
	if (!path) {
		return false;
	}

	*path /= "ShieldFramework.log"sv;
	auto sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(path->string(), true);
#endif

	auto log = std::make_shared<spdlog::logger>("global log"s, std::move(sink));

#ifndef NDEBUG
	log->set_level(spdlog::level::trace);
#else
	log->set_level(spdlog::level::info);
	log->flush_on(spdlog::level::warn);
#endif

	spdlog::set_default_logger(std::move(log));
	spdlog::set_pattern("%g(%#): [%^%l%$] %v"s);

	a_info->infoVersion = F4SE::PluginInfo::kVersion;
	a_info->name = "ShieldFramework";
	a_info->version = 1;

	if (a_f4se->IsEditor()) {
		logger::critical("loaded in editor"sv);
		return false;
	}

	const auto ver = a_f4se->RuntimeVersion();
	if (ver < F4SE::RUNTIME_1_10_162) {
		logger::critical("unsupported runtime v{}"sv, ver.string());
		return false;
	}

	F4SE::AllocTrampoline(8 * 8);

	return true;
}

extern "C" DLLEXPORT bool F4SEAPI F4SEPlugin_Load(const F4SE::LoadInterface* a_f4se)
{
	F4SE::Init(a_f4se);

	F4SE::Trampoline& trampoline = F4SE::GetTrampoline();
	DoHitMeOrig = trampoline.write_call<5>(ptr_DoHitMe.address(), &HookedDoHitMe);
	UpdateSceneGraphOrig = trampoline.write_call<5>(ptr_UpdateSceneGraph.address(), &HookedUpdateSceneGraph);
	Demand3DOrig = trampoline.write_call<5>(ptr_Demand3D.address(), &HookedOMODDemand3D);

	const F4SE::MessagingInterface* message = F4SE::GetMessagingInterface();
	message->RegisterListener([](F4SE::MessagingInterface::Message* msg) -> void {
		if (msg->type == F4SE::MessagingInterface::kGameLoaded) {
			InitializeShieldData();
			InitializeImpactData();
			InitializeFramework();
			EquipWatcher* ew = new EquipWatcher();
			EquipEventSource::GetSingleton()->RegisterSink(ew);
			ObjectLoadWatcher* olw = new ObjectLoadWatcher();
			ObjectLoadedEventSource::GetSingleton()->RegisterSink(olw);
			MenuWatcher* mew = new MenuWatcher();
			UI::GetSingleton()->GetEventSource<MenuOpenCloseEvent>()->RegisterSink(mew);
		}
	});
	return true;
}
