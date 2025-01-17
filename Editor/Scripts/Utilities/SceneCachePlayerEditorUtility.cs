﻿using System;
using System.Collections.Generic;
using System.IO;
using Unity.FilmInternalUtilities;
using Unity.FilmInternalUtilities.Editor;
using UnityEditor;
using UnityEngine;
using UnityEngine.Animations;
using UnityEngine.Assertions;
using UnityEngine.Playables;
using UnityEngine.Timeline;

#if AT_USE_HDRP
using UnityEngine.Rendering.HighDefinition;
#endif

using Object = UnityEngine.Object;

namespace Unity.MeshSync.Editor  {

internal static class SceneCachePlayerEditorUtility {

    internal static bool CreateSceneCachePlayerAndPrefab(string sceneCacheFilePath, 
        string prefabPath, string assetsFolder, 
        out SceneCachePlayer player, out GameObject prefab) 
    {
        player = null;
        prefab = null;
        
        GameObject go = new GameObject();        
        go.name = Path.GetFileNameWithoutExtension(sceneCacheFilePath);

        player = AddSceneCachePlayer(go, sceneCacheFilePath, assetsFolder);
        if (null == player) {
            Object.DestroyImmediate(go);            
            return false;
        }

        //Optimize serialization by ensuring to serialize the key values at the end 
        player.EnableKeyValuesSerialization(false);
        prefab = player.gameObject.SaveAsPrefab(prefabPath);
        if (null == prefab) {
            Object.DestroyImmediate(go);            
            return false;
        }
        
        player.EnableKeyValuesSerialization(true);       
        PrefabUtility.ApplyPrefabInstance(player.gameObject, InteractionMode.AutomatedAction);
        
        Undo.RegisterCreatedObjectUndo(go, "SceneCachePlayer");
        return true;
    }

//----------------------------------------------------------------------------------------------------------------------    
    
    internal static void ChangeSceneCacheFile(SceneCachePlayer cachePlayer, string sceneCacheFilePath) {
        string     prefabPath = null;
        GameObject go         = cachePlayer.gameObject;
        //Check if it's possible to reuse the old assetsFolder
        string assetsFolder = cachePlayer.GetAssetsFolder();
        if (string.IsNullOrEmpty(assetsFolder)) {
            MeshSyncProjectSettings projectSettings = MeshSyncProjectSettings.GetOrCreateInstance();        
            string                  scOutputPath    = projectSettings.GetSceneCacheOutputPath();            
            assetsFolder = Path.Combine(scOutputPath, Path.GetFileNameWithoutExtension(sceneCacheFilePath));
        }
        
        bool isPrefabInstance = cachePlayer.gameObject.IsPrefabInstance();        
        //We are directly modifying a prefab
        if (!isPrefabInstance && go.IsPrefab()) {
            prefabPath = AssetDatabase.GetAssetPath(go);
            CreateSceneCachePlayerAndPrefab(sceneCacheFilePath, prefabPath, assetsFolder, out SceneCachePlayer player,
                out GameObject newPrefab);
            Object.DestroyImmediate(player.gameObject);
            return;

        } 
        
        if (isPrefabInstance) {
            GameObject prefab = PrefabUtility.GetCorrespondingObjectFromSource(cachePlayer.gameObject);
            
            //GetCorrespondingObjectFromSource() may return the ".sc" GameObject instead of the prefab
            //due to the SceneCacheImporter
            string assetPath = AssetDatabase.GetAssetPath(prefab);
            if (Path.GetExtension(assetPath).ToLower() == ".prefab") {
                prefabPath = assetPath;
            } else {
                isPrefabInstance = false;
            }            
        } 
        
        cachePlayer.CloseCache();
        
        //[TODO-sin: 2020-9-28] Find out if it is possible to do undo properly
        Undo.RegisterFullObjectHierarchyUndo(cachePlayer.gameObject, "SceneCachePlayer");
        
        Dictionary<string,EntityRecord> prevRecords = new Dictionary<string, EntityRecord>(cachePlayer.GetClientObjects());        
        
        GameObject tempGO = null;
        Dictionary<Transform, Transform> nonPrefabTrans = new Dictionary<Transform, Transform>(); //nonPrefab -> origParent
        if (isPrefabInstance) {
            //Move non-prefab transforms
            tempGO = new GameObject("Temp");
            FindNonPrefabChildren(cachePlayer.transform, ref nonPrefabTrans);
            nonPrefabTrans.Keys.SetParent(tempGO.transform);
            
            PrefabUtility.UnpackPrefabInstance(cachePlayer.gameObject, PrefabUnpackMode.Completely, InteractionMode.AutomatedAction);                       
        }


        //remove irrelevant  components of the GameObject if the entity type is different
        void ChangeEntityTypeCB(GameObject updatedGo, TransformData data) {
            string dataPath = data.path;
            if (!prevRecords.TryGetValue(dataPath, out EntityRecord prevRecord)) 
                return;

            if (data.entityType == prevRecord.dataType) 
                return;

            DestroyIrrelevantComponents(updatedGo, data.entityType);
        }


        cachePlayer.onUpdateEntity += ChangeEntityTypeCB;        
        cachePlayer.Init(assetsFolder);
        cachePlayer.OpenCacheInEditor(sceneCacheFilePath);        
        cachePlayer.onUpdateEntity -= ChangeEntityTypeCB;
        
        IDictionary<string,EntityRecord> curRecords = cachePlayer.GetClientObjects();
        DeleteInvalidRecordedGameObjects(prevRecords, curRecords);

        if (!isPrefabInstance) {
            return;
        }
        
        
        cachePlayer.gameObject.SaveAsPrefab(prefabPath); //Save as prefab if it was originally a prefab instance
        
        //Move nonPrefab transforms back
        foreach (KeyValuePair<Transform, Transform> kv in nonPrefabTrans) {
            Transform origParent = kv.Value;
            Transform t          = kv.Key;
            if (null == origParent) {
                ObjectUtility.Destroy(t.gameObject);
            } else {
                t.SetParent(origParent);
            } 
        }
        ObjectUtility.Destroy(tempGO);
        
        
    }

    internal static void ReloadSceneCacheFile(SceneCachePlayer cachePlayer) {
        string sceneCacheFilePath = cachePlayer.GetSceneCacheFilePath();        
        ChangeSceneCacheFile(cachePlayer, sceneCacheFilePath);
        
    }
    
//----------------------------------------------------------------------------------------------------------------------    

    internal static TimelineClip AddSceneCacheTrackAndClip(PlayableDirector director, string trackName, 
        SceneCachePlayer sceneCachePlayer) 
    {
        TimelineAsset timelineAsset = director.playableAsset as TimelineAsset;
        Assert.IsNotNull(timelineAsset);
    
        SceneCacheTrack         sceneCacheTrack = timelineAsset.CreateTrack<SceneCacheTrack>(null, trackName);
        TimelineClip            clip            = sceneCacheTrack.CreateDefaultClip();
        SceneCachePlayableAsset playableAsset = clip.asset as SceneCachePlayableAsset;
        Assert.IsNotNull(playableAsset);
        director.SetReferenceValue(playableAsset.GetSceneCachePlayerRef().exposedName, sceneCachePlayer );
        return clip;
    }

//----------------------------------------------------------------------------------------------------------------------    
    
    internal static bool DrawLimitedAnimationGUI(LimitedAnimationController ctrl, 
        Object target, SceneCachePlayer sc) 
    {
        bool         changed   = false;
        const string UNDO_TEXT = "SceneCache: Limited Animation";
        
        //Limited Animation
        changed |= EditorGUIDrawerUtility.DrawUndoableGUI(target, UNDO_TEXT,
            guiFunc: () => (EditorGUILayout.Toggle("Limited Animation", ctrl.IsEnabled())),
            updateFunc: (bool limitedAnimation) => {
                ctrl.SetEnabled(limitedAnimation);
                SceneCachePlayerEditorUtility.RefreshSceneCache(sc);
            });

        ++EditorGUI.indentLevel;
        using (new EditorGUI.DisabledScope(!ctrl.IsEnabled())) {
            changed |= EditorGUIDrawerUtility.DrawUndoableGUI(target, UNDO_TEXT,
                guiFunc: () => (
                    EditorGUILayout.IntField("Num Frames to Hold", ctrl.GetNumFramesToHold())
                ),
                updateFunc: (int frames) => {
                    ctrl.SetNumFramesToHold(frames);
                    SceneCachePlayerEditorUtility.RefreshSceneCache(sc);
                });
            changed |= EditorGUIDrawerUtility.DrawUndoableGUI(target, UNDO_TEXT,
                guiFunc: () => (
                    EditorGUILayout.IntField("Frame Offset", ctrl.GetFrameOffset())
                ),
                updateFunc: (int offset) => {
                    ctrl.SetFrameOffset(offset);
                    SceneCachePlayerEditorUtility.RefreshSceneCache(sc);
                });
        }

        --EditorGUI.indentLevel;

        EditorGUILayout.Space();
        return changed;
    }
    
    internal static void RefreshSceneCache(SceneCachePlayer t) {
        t.ForceUpdate();
        SceneView.RepaintAll();
    }
    
//----------------------------------------------------------------------------------------------------------------------    
    private static void DestroyIrrelevantComponents(GameObject obj, EntityType curEntityType) {

        HashSet<Type> componentsToDelete = new HashSet<Type>(m_componentsToDeleteOnReload);

        //Check which component should remain (should not be deleted)
        switch (curEntityType) {
            case EntityType.Camera:
                componentsToDelete.Remove(typeof(Camera));
                break;
            case EntityType.Light:
                componentsToDelete.Remove(typeof(Light));
                break;
            case EntityType.Mesh: {
                componentsToDelete.Remove(typeof(SkinnedMeshRenderer));
                componentsToDelete.Remove(typeof(MeshFilter));
                componentsToDelete.Remove(typeof(MeshRenderer));
                break;
            }                
            case EntityType.Points:
                componentsToDelete.Remove(typeof(PointCache));
                componentsToDelete.Remove(typeof(PointCacheRenderer));
                break;
            default:
                break;
        }
        

        foreach (Type t in componentsToDelete) {
            Component c = obj.GetComponent(t);
            if (null == c)
                continue;
            
            ObjectUtility.Destroy(c);
        }

    }
    
    //Delete GameObject if they exist in prevRecords, but not in curRecords
    private static void DeleteInvalidRecordedGameObjects(IDictionary<string, EntityRecord> prevRecords,
        IDictionary<string, EntityRecord> curRecords) 
    {
        foreach (KeyValuePair<string, EntityRecord> kv in prevRecords) {
            string       goPath           = kv.Key;
            EntityRecord prevEntityRecord = kv.Value;

            if (curRecords.ContainsKey(goPath)) 
                continue;
            
            ObjectUtility.Destroy(prevEntityRecord.go);
        }
    }

    private static void FindNonPrefabChildren(Transform t, ref Dictionary<Transform, Transform> nonPrefabTransforms) {
        if (!t.gameObject.IsPrefabInstance()) {
            nonPrefabTransforms.Add(t, t.parent);
        }
        
        int numChildren = t.childCount;
        for (int i = 0; i < numChildren; ++i) {
            Transform child = t.GetChild(i);
            FindNonPrefabChildren(child, ref nonPrefabTransforms);
        }
    }
    
//----------------------------------------------------------------------------------------------------------------------    
    private static SceneCachePlayer  AddSceneCachePlayer(GameObject go, 
                                                            string sceneCacheFilePath, 
                                                            string assetsFolder) 
    {
        if (!ValidateSceneCacheOutputPath()) {
            return null;
        }
        
              
        SceneCachePlayer player = go.GetOrAddComponent<SceneCachePlayer>();
        player.Init(assetsFolder);

        if (!player.OpenCacheInEditor(sceneCacheFilePath)) {
            return null;
        }       
        
        return player;
    }


//----------------------------------------------------------------------------------------------------------------------    

    private static bool ValidateSceneCacheOutputPath() {

        MeshSyncProjectSettings projectSettings = MeshSyncProjectSettings.GetOrCreateInstance();
        string                  scOutputPath    = projectSettings.GetSceneCacheOutputPath();
        if (!scOutputPath.StartsWith("Assets")) {
            DisplaySceneCacheOutputPathErrorDialog(scOutputPath);
            return false;            
        }        
        
        try {
            Directory.CreateDirectory(scOutputPath);
        } catch {
            DisplaySceneCacheOutputPathErrorDialog(scOutputPath);
            return false;
        }

        return true;
    }

    private static void DisplaySceneCacheOutputPathErrorDialog(string path) {
        EditorUtility.DisplayDialog("MeshSync",
            $"Invalid SceneCache output path: {path}. " + Environment.NewLine + 
            "Please configure in ProjectSettings", 
            "Ok");
        
    }
    
    
    static readonly HashSet<Type> m_componentsToDeleteOnReload = new HashSet<Type>() {
        typeof(SkinnedMeshRenderer),
        typeof(MeshFilter),
        typeof(MeshRenderer),
        typeof(PointCacheRenderer),
        typeof(PointCache),
        typeof(Camera),
        typeof(Light),
#if AT_USE_HDRP            
        typeof(HDAdditionalLightData),
#endif            

        typeof(AimConstraint),
        typeof(ParentConstraint),
        typeof(PositionConstraint),
        typeof(RotationConstraint),
        typeof(ScaleConstraint),
            
        //typeof(Animator),
        typeof(MeshCollider),
            
    };
    
    
}

} //end namespace