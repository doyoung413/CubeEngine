//Author: DOYEONG LEE
//Project: CubeEngine
//File: ObjectManager.hpp
#pragma once
#include "Object.hpp"

#include <functional>
#include <algorithm>
#include <map>
#include <memory>
#include <vector>
#include <iostream>

class Sprite;
class Physics3D;
class Light;
class ObjectManager
{
public:
    ObjectManager()  = default;
    ~ObjectManager() = default;

	void Update(float dt);
    void DeleteObjectsFromList();
    void End();

    template <typename T, typename... Args>
    void AddObject(Args... args)
    {
        //auto object = std::unique_ptr<T>(std::make_unique<T>(std::forward<Args>(args)...));
        ////T& objectRef = *object.get();
        //static_cast<Object&>(*object.get()).SetId(lastObjectID);

        objectMap[lastObjectID] = std::unique_ptr<T>(std::make_unique<T>(std::forward<Args>(args)...));;
        objectMap[lastObjectID].get()->SetId(lastObjectID);
        ++lastObjectID;
        //return static_cast<T&>(*objectMap[lastObjectID - 1].get());
    }

    void  Draw(float dt);
    void  Destroy(int id);
    void  DestroyAllObjects();

    int   GetLastObjectID();
    std::map<int, std::unique_ptr<Object>>& GetObjectMap() { return objectMap; }

    Object* FindObjectWithName(std::string name);
    Object* FindObjectWithId(int id) { return objectMap.at(id).get(); }
    Object* GetLastObject()
    {
        if (objectMap.empty())
        {
            return nullptr;
        }
        return objectMap.rbegin()->second.get();
    }
    void ObjectControllerForImGui();

    template<typename ComponentTypes, typename Func>
    void QueueComponentFunction(ComponentTypes* component, Func&& func)
    {
        if (component == nullptr)
        {
            std::cerr << "nullptr component!" << '\n';
            return;
        }

        functionQueue.push_back([component, func]()
        {
            func(component);
        });
    }

    template<typename T, typename Func>
    void QueueObjectFunction(T* object, Func&& func) 
    {
        if (object == nullptr) 
        {
            std::cerr << "nullptr object!" << '\n';
            return;
        }
        functionQueue.push_back([object, func]() 
        {
            func(object);
        });
    }

    void ProcessFunctionQueue();

#ifdef _DEBUG
    void SetIsDrawNormals(bool draw) { isDrawNormals = draw; };
#endif
private:
    void Physics3DControllerForImGui(Physics3D* phy);
    void SpriteControllerForImGui(Sprite* sprite);
    void LightControllerForImGui(Light* light);
    void SelectObjectWithMouse();
    void AddComponentPopUpForImGui();
    void SelectObjModelPopUpForImGui();
    int                                    lastObjectID = 0;
    std::map<int, std::unique_ptr<Object>> objectMap;
    std::vector<int>                       objectsToBeDeleted; // list of object id to be deleted

    //For ObjectController
    std::string objName;
    bool isShowPopup = false;
    int selectedItem = -1;

    int objectListForImguiIndex = 0;
    int currentIndex = 0;
	int closestObjectId = 0;
    bool isDragObject = false;
    bool isObjGravityOn = false;

    int stacks = 2;
    int slices = 2;
    float metallic = 0.3f;
    float roughness = 0.3f;
#ifdef _DEBUG
    bool isDrawNormals = false;
#endif

    std::vector<std::function<void()>> functionQueue;
    //For ObjectController
};