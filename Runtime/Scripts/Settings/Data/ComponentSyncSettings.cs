﻿using System;
using UnityEngine;

namespace Unity.MeshSync {

[Serializable]
internal class ComponentSyncSettings {

    internal ComponentSyncSettings() {
        
    }

    internal ComponentSyncSettings(ComponentSyncSettings other) {
        this.CanCreate = other.CanCreate;
        this.CanUpdate = other.CanUpdate;
    }

//----------------------------------------------------------------------------------------------------------------------
    
    [SerializeField] internal bool CanCreate = true;
    [SerializeField] internal bool CanUpdate = true;
    
}

} //end namespace