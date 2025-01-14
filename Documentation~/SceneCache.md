# Scene Cache

Scene Cache is a feature to playback all frames of an *.sc* file that 
was exported using [MeshSyncDCCPlugins](https://docs.unity3d.com/Packages/com.unity.meshsync.dcc-plugins@latest)
installed in a DCC Tool.   
This functionality is very similar to [AlembicForUnity](https://docs.unity3d.com/Packages/com.unity.formats.alembic@latest/index.html),
but it has the following differences:

1. Scene Cache is designed to playback frames precisely with high performance.
2. Scene Cache supports material export/import
3. Unlike Alembic, *.sc* files are only playable in Unity.

## How to use

From the menu, select **Game Object > MeshSync > Create Cache Player**, 
and then select a previously exported *.sc* file, 
which can be either inside or outside the Unity project.  
This will automatically create a GameObject with 
[SceneCachePlayer](#scene-cache-player) component, 
which will be played automatically in PlayMode.

![Menu](images/MenuCreateCachePlayer.png)

Normally, the playback is controlled using an 
[*Animator*](https://docs.unity3d.com/ScriptReference/Animator.html) with an 
[*AnimationClip*](https://docs.unity3d.com/ScriptReference/AnimationClip.html), but 
we can also control the playback of [Scene Cache in Timeline](SceneCacheInTimeline.md).

## Scene Cache Importer 

![](images/SceneCacheImporter.png)

When an *.sc* file is inside the Unity project, clicking on the *.sc* file 
will open its import settings in the inspector.
These import settings are similar to the [import properties](CommonMeshSyncProperties.md#import) in the inspector, 
and can be overridden in the inspector as well.

## Scene Cache Player 

This component handles the playback of an *.sc* file. 

### Properties

![](images/SceneCachePlayer.png)

- **Cache File Path**: the path to the *.sc* file.  
  Copying the cache file to StreamingAssets is recommended, and can be done by simply clicking the **Copy** button.  
 
  > Playing *.sc* files located in folders outside the active Unity project is supported, 
  > but keep in mind that only the computer which stores those *.sc* files can play them.

- **Info**: basic information of the *.sc* file.
- **Playback Mode**  

  |**Playback Mode**           |**Description** |
  |:----------------------- |:---|
  | **Snap to Previous Frame** | Selects the last frame which has been passed by the playback time. |
  | **Snap to Nearest Frame** (Default) | Selects the frame which is nearest to the playback time. |
  | **Interpolation**          | Smoothens animations by interpolating meshes and transforms between nearest neighboring frames. <br/> Note that meshes are only interpolated if the topologies match (the vertex indexes remain unchanged). |

  - **Time**: the playback time. 
  - **Frame**: the selected frame of the *.sc* file.   
    Disabled if the **Playback Mode** is set to **Interpolation**.

- **Limited Animation**: skips *.sc* frames during playback if enabled.
  - **Num Frames to Hold**: the duration in number of frames that a selected frame will be hold.
  - **Frame Offset**: an offset value to change which frames get selected when applying **Limited Animation**.

Please refer to [common properties](CommonMeshSyncProperties.md) for details on the other properties.

### Tips

* **Material List** property can be used to carry over existing materials when the cache file is updated.



