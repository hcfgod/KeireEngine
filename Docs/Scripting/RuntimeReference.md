# Runtime API Member Reference

[Scripting home](README.md) · [Workflow map](WorkflowMap.md) · [Cookbook](Cookbook.md) · [API index](ApiIndex.md)

These declarations belong to `Keire.Managed.dll`. Read the system guides for timing, ownership, validation, and examples; a public declaration alone does not guarantee a host supplies that service. [Capability status](ManagedApiMatrix.md) distinguishes supported workflows from remaining work.

## How To Read This Reference

Search for a type or member name, or select a type below. Each entry links to its defining source. Signatures retain parameter names, defaults, generic constraints, attributes, and accessor visibility. Method bodies and field/property initializers are omitted. Base types supply inherited members; follow their entries when a method is not declared on a derived type. Partial declarations are combined under one entry. Positional record parameters also declare record properties; compiler-generated equality, deconstruction, and implicit constructors are not repeated.

Protected members are subclass extension points. `internal` and `private` members are excluded. Public bridge interfaces, identity records, serialization codecs, and `Runtime*` methods/fields are advanced host/integration surfaces: ordinary scripts use the object and component APIs instead of installing bridges, invoking runtime lifecycle dispatch, or editing diagnostic transport fields.

This is a generated declaration catalog, not a replacement for the workflow guides. Regenerate or check it using [the documented commands](ApiIndex.md#maintaining-the-member-reference).

## Type Directory

- [`Keire.AnimationClip`](#keireanimationclip)
- [`Keire.AnimationEvent`](#keireanimationevent)
- [`Keire.AnimationIkContext`](#keireanimationikcontext)
- [`Keire.AnimationSource`](#keireanimationsource)
- [`Keire.Animator`](#keireanimator)
- [`Keire.AnimatorController`](#keireanimatorcontroller)
- [`Keire.AnimatorIkSpace`](#keireanimatorikspace)
- [`Keire.AnimatorStateInfo`](#keireanimatorstateinfo)
- [`Keire.Application`](#keireapplication)
- [`Keire.Asset`](#keireasset)
- [`Keire.AssetId`](#keireassetid)
- [`Keire.AssetLoadDiagnostic`](#keireassetloaddiagnostic)
- [`Keire.AssetLoadException`](#keireassetloadexception)
- [`Keire.AssetLoadOperation<T>`](#keireassetloadoperation-of-t)
- [`Keire.AssetLoadPriority`](#keireassetloadpriority)
- [`Keire.AssetLoadState`](#keireassetloadstate)
- [`Keire.Assets`](#keireassets)
- [`Keire.Audio`](#keireaudio)
- [`Keire.AudioClip`](#keireaudioclip)
- [`Keire.AudioListener`](#keireaudiolistener)
- [`Keire.AudioMixer`](#keireaudiomixer)
- [`Keire.AudioPlaybackOptions`](#keireaudioplaybackoptions)
- [`Keire.AudioPlaybackState`](#keireaudioplaybackstate)
- [`Keire.AudioReverbZone`](#keireaudioreverbzone)
- [`Keire.AudioReverbZoneShape`](#keireaudioreverbzoneshape)
- [`Keire.AudioSource`](#keireaudiosource)
- [`Keire.AudioSourceStatus`](#keireaudiosourcestatus)
- [`Keire.AvatarMask`](#keireavatarmask)
- [`Keire.AxisControl`](#keireaxiscontrol)
- [`Keire.Behaviour`](#keirebehaviour)
- [`Keire.BinaryAsset`](#keirebinaryasset)
- [`Keire.ButtonControl`](#keirebuttoncontrol)
- [`Keire.Camera`](#keirecamera)
- [`Keire.CameraClearMode`](#keirecameraclearmode)
- [`Keire.CameraProjection`](#keirecameraprojection)
- [`Keire.CharacterController`](#keirecharactercontroller)
- [`Keire.CharacterControllerState`](#keirecharactercontrollerstate)
- [`Keire.Collider`](#keirecollider)
- [`Keire.ColliderShape`](#keirecollidershape)
- [`Keire.CollisionContact`](#keirecollisioncontact)
- [`Keire.Color`](#keirecolor)
- [`Keire.Component`](#keirecomponent)
- [`Keire.ComponentType`](#keirecomponenttype)
- [`Keire.ComponentTypeId`](#keirecomponenttypeid)
- [`Keire.ComputeBackend`](#keirecomputebackend)
- [`Keire.ComputeBuffer`](#keirecomputebuffer)
- [`Keire.ComputeBufferBinding`](#keirecomputebufferbinding)
- [`Keire.ComputeDevice`](#keirecomputedevice)
- [`Keire.ComputePipeline`](#keirecomputepipeline)
- [`Keire.ComputeSubmission`](#keirecomputesubmission)
- [`Keire.Coroutine`](#keirecoroutine)
- [`Keire.CreateAssetMenuAttribute`](#keirecreateassetmenuattribute)
- [`Keire.Cursor`](#keirecursor)
- [`Keire.CustomManagedValueConverterAttribute`](#keirecustommanagedvalueconverterattribute)
- [`Keire.CustomYieldInstruction`](#keirecustomyieldinstruction)
- [`Keire.Debug`](#keiredebug)
- [`Keire.DirectionalLight`](#keiredirectionallight)
- [`Keire.DistanceJoint`](#keiredistancejoint)
- [`Keire.DynamicMaterial`](#keiredynamicmaterial)
- [`Keire.EngineObject`](#keireengineobject)
- [`Keire.Entity`](#keireentity)
- [`Keire.EntityId`](#keireentityid)
- [`Keire.ExecutionOrderAttribute`](#keireexecutionorderattribute)
- [`Keire.FixedJoint`](#keirefixedjoint)
- [`Keire.ForceMode`](#keireforcemode)
- [`Keire.FormerlySerializedAsAttribute`](#keireformerlyserializedasattribute)
- [`Keire.FullscreenMode`](#keirefullscreenmode)
- [`Keire.GIReceiveMode`](#keiregireceivemode)
- [`Keire.Gamepad`](#keiregamepad)
- [`Keire.GlobalMaterialParameters`](#keireglobalmaterialparameters)
- [`Keire.HeaderAttribute`](#keireheaderattribute)
- [`Keire.HideInInspectorAttribute`](#keirehideininspectorattribute)
- [`Keire.HingeJoint`](#keirehingejoint)
- [`Keire.HotReloadStateAttribute`](#keirehotreloadstateattribute)
- [`Keire.IManagedValueMigration`](#keireimanagedvaluemigration)
- [`Keire.IRuntimeBridge`](#keireiruntimebridge)
- [`Keire.IRuntimeService`](#keireiruntimeservice)
- [`Keire.IRuntimeServiceHotReloadState`](#keireiruntimeservicehotreloadstate)
- [`Keire.ISerializationCallbackReceiver`](#keireiserializationcallbackreceiver)
- [`Keire.Input`](#keireinput)
- [`Keire.Input.Gamepad`](#keireinputgamepad)
- [`Keire.Input.Keyboard`](#keireinputkeyboard)
- [`Keire.Input.Mouse`](#keireinputmouse)
- [`Keire.InputAction`](#keireinputaction)
- [`Keire.InputAction.CallbackContext`](#keireinputactioncallbackcontext)
- [`Keire.InputActionAsset`](#keireinputactionasset)
- [`Keire.InputActionContext`](#keireinputactioncontext)
- [`Keire.InputActionMap`](#keireinputactionmap)
- [`Keire.InputActionPhase`](#keireinputactionphase)
- [`Keire.InputControl`](#keireinputcontrol)
- [`Keire.InputDevice`](#keireinputdevice)
- [`Keire.InputDeviceMask`](#keireinputdevicemask)
- [`Keire.InputDeviceType`](#keireinputdevicetype)
- [`Keire.InputRebindOperation`](#keireinputrebindoperation)
- [`Keire.InputRebindOptions`](#keireinputrebindoptions)
- [`Keire.InputRebindResolution`](#keireinputrebindresolution)
- [`Keire.InputRebindSnapshot`](#keireinputrebindsnapshot)
- [`Keire.InputRebindStatus`](#keireinputrebindstatus)
- [`Keire.InputValueType`](#keireinputvaluetype)
- [`Keire.InspectorGroupAttribute`](#keireinspectorgroupattribute)
- [`Keire.InspectorNameAttribute`](#keireinspectornameattribute)
- [`Keire.InspectorStepAttribute`](#keireinspectorstepattribute)
- [`Keire.Job`](#keirejob)
- [`Keire.JobClass`](#keirejobclass)
- [`Keire.JobContext`](#keirejobcontext)
- [`Keire.JobDescription`](#keirejobdescription)
- [`Keire.JobPriority`](#keirejobpriority)
- [`Keire.JobStatus`](#keirejobstatus)
- [`Keire.Jobs`](#keirejobs)
- [`Keire.Joint`](#keirejoint)
- [`Keire.KeireEvent`](#keirekeireevent)
- [`Keire.KeireEvent<T0, T1, T2, T3>`](#keirekeireevent-of-t0-and-t1-and-t2-and-t3)
- [`Keire.KeireEvent<T0, T1, T2>`](#keirekeireevent-of-t0-and-t1-and-t2)
- [`Keire.KeireEvent<T0, T1>`](#keirekeireevent-of-t0-and-t1)
- [`Keire.KeireEvent<T0>`](#keirekeireevent-of-t0)
- [`Keire.KeireEventBase`](#keirekeireeventbase)
- [`Keire.Key`](#keirekey)
- [`Keire.Keyboard`](#keirekeyboard)
- [`Keire.LightBakeMode`](#keirelightbakemode)
- [`Keire.LightProbeData`](#keirelightprobedata)
- [`Keire.LightProbeVolume`](#keirelightprobevolume)
- [`Keire.LightingQuality`](#keirelightingquality)
- [`Keire.LightingSet`](#keirelightingset)
- [`Keire.LightingTextureArray`](#keirelightingtexturearray)
- [`Keire.LimbContactEvent`](#keirelimbcontactevent)
- [`Keire.LimbContactEventKind`](#keirelimbcontacteventkind)
- [`Keire.LimbContactTracker`](#keirelimbcontacttracker)
- [`Keire.LimbGaitGroup`](#keirelimbgaitgroup)
- [`Keire.LimbGaitScheduler`](#keirelimbgaitscheduler)
- [`Keire.LimbId`](#keirelimbid)
- [`Keire.LimbIkDefinition`](#keirelimbikdefinition)
- [`Keire.LimbIkResult`](#keirelimbikresult)
- [`Keire.LimbIkRig`](#keirelimbikrig)
- [`Keire.LimbIkSolveStatus`](#keirelimbiksolvestatus)
- [`Keire.LimbIkSolver`](#keirelimbiksolver)
- [`Keire.LimbIkTarget`](#keirelimbiktarget)
- [`Keire.LimbSupportBalance`](#keirelimbsupportbalance)
- [`Keire.LimbSupportBalanceResult`](#keirelimbsupportbalanceresult)
- [`Keire.LimbSupportBalanceStatus`](#keirelimbsupportbalancestatus)
- [`Keire.LimbSupportContinuity`](#keirelimbsupportcontinuity)
- [`Keire.LimbSupportLoss`](#keirelimbsupportloss)
- [`Keire.LimbSupportMotion`](#keirelimbsupportmotion)
- [`Keire.LimbSupportPoint`](#keirelimbsupportpoint)
- [`Keire.LimbSupportPose`](#keirelimbsupportpose)
- [`Keire.LimbSupportRequirement`](#keirelimbsupportrequirement)
- [`Keire.Log`](#keirelog)
- [`Keire.LogLevel`](#keireloglevel)
- [`Keire.ManagedMigrationContext`](#keiremanagedmigrationcontext)
- [`Keire.ManagedSerialization`](#keiremanagedserialization)
- [`Keire.ManagedSerializationContext`](#keiremanagedserializationcontext)
- [`Keire.ManagedSerializationException`](#keiremanagedserializationexception)
- [`Keire.ManagedSerializedValue`](#keiremanagedserializedvalue)
- [`Keire.ManagedSerializedValueKind`](#keiremanagedserializedvaluekind)
- [`Keire.ManagedValueConverter`](#keiremanagedvalueconverter)
- [`Keire.ManagedValueConverter<T>`](#keiremanagedvalueconverter-of-t)
- [`Keire.ManagedValueMigrationAttribute`](#keiremanagedvaluemigrationattribute)
- [`Keire.Material`](#keirematerial)
- [`Keire.MaterialFunction`](#keirematerialfunction)
- [`Keire.MaterialGraph`](#keirematerialgraph)
- [`Keire.MaterialInstance`](#keirematerialinstance)
- [`Keire.MaterialLayer`](#keiremateriallayer)
- [`Keire.MaterialLayerBlend`](#keiremateriallayerblend)
- [`Keire.MaterialParameterCollection`](#keirematerialparametercollection)
- [`Keire.MaterialParameterCollectionInstance`](#keirematerialparametercollectioninstance)
- [`Keire.MaterialPropertyBlock`](#keirematerialpropertyblock)
- [`Keire.MaxAttribute`](#keiremaxattribute)
- [`Keire.Mesh`](#keiremesh)
- [`Keire.MeshRenderer`](#keiremeshrenderer)
- [`Keire.MinAttribute`](#keireminattribute)
- [`Keire.Mouse`](#keiremouse)
- [`Keire.MultilineAttribute`](#keiremultilineattribute)
- [`Keire.NativeAbiValue`](#keirenativeabivalue)
- [`Keire.NativeBufferAttribute`](#keirenativebufferattribute)
- [`Keire.NativeCallError`](#keirenativecallerror)
- [`Keire.NativeCallResult<T>`](#keirenativecallresult-of-t)
- [`Keire.NativeMethodAttribute`](#keirenativemethodattribute)
- [`Keire.NativeMethodDescriptor`](#keirenativemethoddescriptor)
- [`Keire.NativeParameterDescriptor`](#keirenativeparameterdescriptor)
- [`Keire.NativeServiceContractAttribute`](#keirenativeservicecontractattribute)
- [`Keire.NativeServiceDescriptor`](#keirenativeservicedescriptor)
- [`Keire.NativeServiceRuntime`](#keirenativeserviceruntime)
- [`Keire.NativeThreadAffinity`](#keirenativethreadaffinity)
- [`Keire.NativeValueKind`](#keirenativevaluekind)
- [`Keire.Navigation`](#keirenavigation)
- [`Keire.NavigationPath`](#keirenavigationpath)
- [`Keire.PersistentEventCall`](#keirepersistenteventcall)
- [`Keire.Physics`](#keirephysics)
- [`Keire.PhysicsMaterial`](#keirephysicsmaterial)
- [`Keire.PlayerPreferences`](#keireplayerpreferences)
- [`Keire.PointLight`](#keirepointlight)
- [`Keire.Prefab`](#keireprefab)
- [`Keire.PresentMode`](#keirepresentmode)
- [`Keire.ProceduralFootSide`](#keireproceduralfootside)
- [`Keire.ProceduralLocomotionIntent`](#keireprocedurallocomotionintent)
- [`Keire.ProceduralLocomotionState`](#keireprocedurallocomotionstate)
- [`Keire.ProceduralMotionEvent`](#keireproceduralmotionevent)
- [`Keire.ProceduralMotionEventType`](#keireproceduralmotioneventtype)
- [`Keire.ProceduralMotionProfile`](#keireproceduralmotionprofile)
- [`Keire.ProceduralMotionQuality`](#keireproceduralmotionquality)
- [`Keire.ProceduralMotionState`](#keireproceduralmotionstate)
- [`Keire.ProfileSample`](#keireprofilesample)
- [`Keire.Profiler`](#keireprofiler)
- [`Keire.Quaternion`](#keirequaternion)
- [`Keire.RangeAttribute`](#keirerangeattribute)
- [`Keire.RaycastHit`](#keireraycasthit)
- [`Keire.ReadOnlyInInspectorAttribute`](#keirereadonlyininspectorattribute)
- [`Keire.ReflectionProbe`](#keirereflectionprobe)
- [`Keire.ReflectionProbeCaptureMode`](#keirereflectionprobecapturemode)
- [`Keire.ReflectionProbeResolution`](#keirereflectionproberesolution)
- [`Keire.RenderEnvironmentSettings`](#keirerenderenvironmentsettings)
- [`Keire.RenderSettings`](#keirerendersettings)
- [`Keire.RequireComponentAttribute`](#keirerequirecomponentattribute)
- [`Keire.Resolution`](#keireresolution)
- [`Keire.RigDefinition`](#keirerigdefinition)
- [`Keire.RigidBody`](#keirerigidbody)
- [`Keire.RigidBodyMotion`](#keirerigidbodymotion)
- [`Keire.RigidBodyProperties`](#keirerigidbodyproperties)
- [`Keire.RuntimeBridge`](#keireruntimebridge)
- [`Keire.RuntimeServiceAttribute`](#keireruntimeserviceattribute)
- [`Keire.RuntimeServiceContext`](#keireruntimeservicecontext)
- [`Keire.RuntimeServiceDependencyAttribute`](#keireruntimeservicedependencyattribute)
- [`Keire.RuntimeServiceDiagnostic`](#keireruntimeservicediagnostic)
- [`Keire.RuntimeServiceHost`](#keireruntimeservicehost)
- [`Keire.RuntimeServiceUpdateContext`](#keireruntimeserviceupdatecontext)
- [`Keire.Scene`](#keirescene)
- [`Keire.SceneAsset`](#keiresceneasset)
- [`Keire.SceneLoadMode`](#keiresceneloadmode)
- [`Keire.SceneLoadOperation`](#keiresceneloadoperation)
- [`Keire.SceneLoadState`](#keiresceneloadstate)
- [`Keire.SceneManager`](#keirescenemanager)
- [`Keire.SceneQuery`](#keirescenequery)
- [`Keire.SceneQueryScope`](#keirescenequeryscope)
- [`Keire.Screen`](#keirescreen)
- [`Keire.ScreenRect`](#keirescreenrect)
- [`Keire.ScriptableObject`](#keirescriptableobject)
- [`Keire.SerializableTypeAttribute`](#keireserializabletypeattribute)
- [`Keire.SerializeFieldAttribute`](#keireserializefieldattribute)
- [`Keire.SerializeReferenceAttribute`](#keireserializereferenceattribute)
- [`Keire.Shader`](#keireshader)
- [`Keire.ShaderFunction`](#keireshaderfunction)
- [`Keire.ShaderGraph`](#keireshadergraph)
- [`Keire.ShaderGraphInstance`](#keireshadergraphinstance)
- [`Keire.ShadowQuality`](#keireshadowquality)
- [`Keire.ShadowResolution`](#keireshadowresolution)
- [`Keire.Skeleton`](#keireskeleton)
- [`Keire.SkinnedMesh`](#keireskinnedmesh)
- [`Keire.SpotLight`](#keirespotlight)
- [`Keire.SpringJoint`](#keirespringjoint)
- [`Keire.StableAssetTypeIdAttribute`](#keirestableassettypeidattribute)
- [`Keire.StableComponentIdAttribute`](#keirestablecomponentidattribute)
- [`Keire.StableFieldIdAttribute`](#keirestablefieldidattribute)
- [`Keire.StableSerializedTypeIdAttribute`](#keirestableserializedtypeidattribute)
- [`Keire.TextAsset`](#keiretextasset)
- [`Keire.Texture`](#keiretexture)
- [`Keire.Time`](#keiretime)
- [`Keire.TooltipAttribute`](#keiretooltipattribute)
- [`Keire.Transform`](#keiretransform)
- [`Keire.UI.Align`](#keireuialign)
- [`Keire.UI.BackgroundFit`](#keireuibackgroundfit)
- [`Keire.UI.BackgroundRepeat`](#keireuibackgroundrepeat)
- [`Keire.UI.BaseField<T>`](#keireuibasefield-of-t)
- [`Keire.UI.BindableElement`](#keireuibindableelement)
- [`Keire.UI.BindingDiagnostic`](#keireuibindingdiagnostic)
- [`Keire.UI.BindingMode`](#keireuibindingmode)
- [`Keire.UI.BoxShadow`](#keireuiboxshadow)
- [`Keire.UI.Button`](#keireuibutton)
- [`Keire.UI.ChangeEvent<T>`](#keireuichangeevent-of-t)
- [`Keire.UI.ClickEvent`](#keireuiclickevent)
- [`Keire.UI.DataBinding`](#keireuidatabinding)
- [`Keire.UI.DisplayStyle`](#keireuidisplaystyle)
- [`Keire.UI.DropdownField`](#keireuidropdownfield)
- [`Keire.UI.EventBase`](#keireuieventbase)
- [`Keire.UI.FlexDirection`](#keireuiflexdirection)
- [`Keire.UI.FocusInEvent`](#keireuifocusinevent)
- [`Keire.UI.FocusOutEvent`](#keireuifocusoutevent)
- [`Keire.UI.Foldout`](#keireuifoldout)
- [`Keire.UI.FontFamily`](#keireuifontfamily)
- [`Keire.UI.FontSlant`](#keireuifontslant)
- [`Keire.UI.ICollectionVirtualizationController`](#keireuiicollectionvirtualizationcontroller)
- [`Keire.UI.Image`](#keireuiimage)
- [`Keire.UI.Justify`](#keireuijustify)
- [`Keire.UI.KeyDownEvent`](#keireuikeydownevent)
- [`Keire.UI.Label`](#keireuilabel)
- [`Keire.UI.ListView`](#keireuilistview)
- [`Keire.UI.NavigationDirection`](#keireuinavigationdirection)
- [`Keire.UI.NavigationMoveEvent`](#keireuinavigationmoveevent)
- [`Keire.UI.Overflow`](#keireuioverflow)
- [`Keire.UI.PanelSettings`](#keireuipanelsettings)
- [`Keire.UI.PointerDownEvent`](#keireuipointerdownevent)
- [`Keire.UI.PointerEventBase`](#keireuipointereventbase)
- [`Keire.UI.PointerMoveEvent`](#keireuipointermoveevent)
- [`Keire.UI.PointerUpEvent`](#keireuipointerupevent)
- [`Keire.UI.Position`](#keireuiposition)
- [`Keire.UI.ProgressBar`](#keireuiprogressbar)
- [`Keire.UI.PropagationPhase`](#keireuipropagationphase)
- [`Keire.UI.RuntimeVisualElement`](#keireuiruntimevisualelement)
- [`Keire.UI.RuntimeVisualElementType`](#keireuiruntimevisualelementtype)
- [`Keire.UI.ScrollView`](#keireuiscrollview)
- [`Keire.UI.Slider`](#keireuislider)
- [`Keire.UI.Style`](#keireuistyle)
- [`Keire.UI.StyleCorners<T>`](#keireuistylecorners-of-t)
- [`Keire.UI.StyleEdges<T>`](#keireuistyleedges-of-t)
- [`Keire.UI.StyleSheet`](#keireuistylesheet)
- [`Keire.UI.SubmitEvent`](#keireuisubmitevent)
- [`Keire.UI.TabView`](#keireuitabview)
- [`Keire.UI.TemplateContainer`](#keireuitemplatecontainer)
- [`Keire.UI.TextDirection`](#keireuitextdirection)
- [`Keire.UI.TextElement`](#keireuitextelement)
- [`Keire.UI.TextField`](#keireuitextfield)
- [`Keire.UI.TextInputEvent`](#keireuitextinputevent)
- [`Keire.UI.TextOverflow`](#keireuitextoverflow)
- [`Keire.UI.TextWrap`](#keireuitextwrap)
- [`Keire.UI.Toggle`](#keireuitoggle)
- [`Keire.UI.Toolbar`](#keireuitoolbar)
- [`Keire.UI.TransitionEasing`](#keireuitransitioneasing)
- [`Keire.UI.TreeView`](#keireuitreeview)
- [`Keire.UI.TrickleDown`](#keireuitrickledown)
- [`Keire.UI.UIDocument`](#keireuiuidocument)
- [`Keire.UI.UQueryBuilder<T>`](#keireuiuquerybuilder-of-t)
- [`Keire.UI.UxmlAttributeAttribute`](#keireuiuxmlattributeattribute)
- [`Keire.UI.UxmlAttributeDescriptor`](#keireuiuxmlattributedescriptor)
- [`Keire.UI.UxmlElementAttribute`](#keireuiuxmlelementattribute)
- [`Keire.UI.UxmlElementDescriptor`](#keireuiuxmlelementdescriptor)
- [`Keire.UI.UxmlElementRegistry`](#keireuiuxmlelementregistry)
- [`Keire.UI.VisualElement`](#keireuivisualelement)
- [`Keire.UI.VisualTreeAsset`](#keireuivisualtreeasset)
- [`Keire.UI.Wrap`](#keireuiwrap)
- [`Keire.Vector2`](#keirevector2)
- [`Keire.Vector2Control`](#keirevector2control)
- [`Keire.Vector3`](#keirevector3)
- [`Keire.Vector4`](#keirevector4)
- [`Keire.Vfx`](#keirevfx)
- [`Keire.VfxEffect`](#keirevfxeffect)
- [`Keire.VfxEmitter`](#keirevfxemitter)
- [`Keire.VfxRange<T>`](#keirevfxrange-of-t)
- [`Keire.VfxSubgraph`](#keirevfxsubgraph)
- [`Keire.VfxVolume`](#keirevfxvolume)
- [`Keire.WaitForEndOfFrame`](#keirewaitforendofframe)
- [`Keire.WaitForFixedUpdate`](#keirewaitforfixedupdate)
- [`Keire.WaitForSeconds`](#keirewaitforseconds)
- [`Keire.WaitForSecondsRealtime`](#keirewaitforsecondsrealtime)
- [`Keire.WaitUntil`](#keirewaituntil)
- [`Keire.WaitWhile`](#keirewaitwhile)
- [`Keire.YieldInstruction`](#keireyieldinstruction)

## Keire.AnimationClip

Sources: [RuntimeApi.cs](../../KeireManaged/RuntimeApi.cs)

```csharp
[StableAssetTypeId("4b454952-4541-4e49-4d43-4c4950000001")]
public sealed class AnimationClip : Asset
{
}
```

## Keire.AnimationEvent

Sources: [Behaviour.cs](../../KeireManaged/Behaviour.cs)

```csharp
public readonly record struct AnimationEvent(string Name, float NormalizedTime, int Integer, float Scalar, string Text)
{
}
```

## Keire.AnimationIkContext

Sources: [Behaviour.cs](../../KeireManaged/Behaviour.cs)

```csharp
public readonly record struct AnimationIkContext(float LayerWeight)
{
    public float InterpolationAlpha { get; init; }
    public bool IsFixedUpdate { get; init; }
}
```

## Keire.AnimationSource

Sources: [NativeAssets.cs](../../KeireManaged/NativeAssets.cs)

```csharp
[StableAssetTypeId("4b454952-4541-4e49-4d53-4f5552434501")]
public sealed class AnimationSource : Asset
{
}
```

## Keire.Animator

Sources: [RuntimeApi.cs](../../KeireManaged/RuntimeApi.cs)

```csharp
[StableComponentId("4b454952-4541-4e49-4d41-544f52000001")]
public sealed class Animator : Component
{
    public bool IsPlaying { get; }
    public bool IsPaused { get; }
    public string CurrentState { get; }
    public float NormalizedTime { get; }
    public float Speed { get; set; }
    public AnimatorStateInfo StateInfo { get; }
    public void Play(string state, float normalizedTime = 0.0f, string? layer = null);
    public void CrossFade(string state, float duration, float normalizedTime = 0.0f, string? layer = null);
    public void Pause();
    public void Resume();
    public void Stop();
    public void SetFootGroundingWeight(float weight);
    public void SetProceduralLocomotion(ProceduralLocomotionIntent intent);
    public ProceduralLocomotionState ProceduralState { get; }
    public void SetFloat(string parameter, float value);
    public void SetInteger(string parameter, int value);
    public void SetBool(string parameter, bool value);
    public void SetTrigger(string parameter);
    public void ResetTrigger(string parameter);
    public void SetLayerWeight(string layer, float value);
    public float GetFloat(string parameter);
    public bool TryGetFloat(string parameter, out float value);
    public int GetInteger(string parameter);
    public bool TryGetInteger(string parameter, out int value);
    public bool GetBool(string parameter);
    public bool TryGetBool(string parameter, out bool value);
    public float GetLayerWeight(string layer);
    public bool TryGetLayerWeight(string layer, out float value);
    public void SetTwoBoneIK(string goal, string rootBone, string middleBone, string endBone, Vector3 target, Vector3 pole, float weight = 1.0f, AnimatorIkSpace space = AnimatorIkSpace.World);
    public void SetFabrikIK(string goal, IReadOnlyList<string> bones, Vector3 target, float weight = 1.0f, uint maximumIterations = 12, float tolerance = 0.001f, AnimatorIkSpace space = AnimatorIkSpace.World);
    public bool ClearIK(string goal);
    public void SetLimbIK(LimbId limb, Vector3 target, Vector3 pole, float weight = 1.0f, AnimatorIkSpace space = AnimatorIkSpace.World, bool enabled = true);
    public bool ClearLimbIK(LimbId limb);
    public bool TryGetLimbIKResult(LimbId limb, out LimbIkResult result);
}
```

## Keire.AnimatorController

Sources: [RuntimeApi.cs](../../KeireManaged/RuntimeApi.cs)

```csharp
[StableAssetTypeId("4b454952-4541-4e49-4d47-524150480001")]
public sealed class AnimatorController : Asset
{
}
```

## Keire.AnimatorIkSpace

Sources: [RuntimeApi.cs](../../KeireManaged/RuntimeApi.cs)

```csharp
public enum AnimatorIkSpace : byte
{
    Model,
    World,
    PresentationWorld
}
```

## Keire.AnimatorStateInfo

Sources: [RuntimeApi.cs](../../KeireManaged/RuntimeApi.cs)

```csharp
public readonly record struct AnimatorStateInfo(string State, float NormalizedTime, bool IsPlaying, bool IsPaused, float Speed)
{
}
```

## Keire.Application

Sources: [RuntimeFoundation.cs](../../KeireManaged/RuntimeFoundation.cs)

```csharp
public static class Application
{
    public static string ProductName { get; }
    public static string Version { get; }
    public static string Identifier { get; }
    public static string PersistentDataPath { get; }
    public static bool IsEditor { get; }
    public static void Quit(int exitCode = 0);
}
```

## Keire.Asset

Sources: [Handles.cs](../../KeireManaged/Handles.cs)

```csharp
public abstract class Asset : EngineObject, IEquatable<Asset>
{
    public AssetId Id { get; }
    public bool IsPersistent { get; }
    public override bool IsValid { get; }
    public bool Equals(Asset? other);
    public override bool Equals(object? value);
    public override int GetHashCode();
    public static bool operator ==(Asset? left, Asset? right);
    public static bool operator !=(Asset? left, Asset? right);
}
```

## Keire.AssetId

Sources: [Handles.cs](../../KeireManaged/Handles.cs)

```csharp
public readonly record struct AssetId(ulong High, ulong Low)
{
    public bool IsValid { get; }
}
```

## Keire.AssetLoadDiagnostic

Sources: [RuntimeAssets.cs](../../KeireManaged/RuntimeAssets.cs)

```csharp
public readonly record struct AssetLoadDiagnostic(string Operation, string Message)
{
    public bool IsEmpty { get; }
}
```

## Keire.AssetLoadException

Sources: [RuntimeAssets.cs](../../KeireManaged/RuntimeAssets.cs)

```csharp
public sealed class AssetLoadException : InvalidOperationException
{
    public AssetId Asset { get; }
    public AssetLoadDiagnostic Diagnostic { get; }
}
```

## Keire.AssetLoadOperation of T

Sources: [RuntimeAssets.cs](../../KeireManaged/RuntimeAssets.cs)

```csharp
public sealed class AssetLoadOperation<T> : CustomYieldInstruction, IDisposable where T : Asset
{
    public T Asset { get; }
    public bool IsValid { get; }
    public AssetLoadState State { get; }
    public bool IsReady { get; }
    public bool IsDone { get; }
    public bool UsingFallback { get; }
    public ulong Revision { get; }
    public AssetLoadDiagnostic Diagnostic { get; }
    public override bool KeepWaiting { get; }
    public void RequireReady();
    public async ValueTask WaitUntilReadyAsync(CancellationToken cancellation = default);
    public void Dispose();
}
```

## Keire.AssetLoadPriority

Sources: [RuntimeAssets.cs](../../KeireManaged/RuntimeAssets.cs)

```csharp
public enum AssetLoadPriority : byte
{
    Critical,
    High,
    Normal,
    Low,
    Background
}
```

## Keire.AssetLoadState

Sources: [RuntimeAssets.cs](../../KeireManaged/RuntimeAssets.cs)

```csharp
public enum AssetLoadState : byte
{
    Queued,
    Loading,
    Ready,
    Reloading,
    Failed,
    Cancelled
}
```

## Keire.Assets

Sources: [ManagedAssetRuntime.cs](../../KeireManaged/ManagedAssetRuntime.cs)

```csharp
public static class Assets
{
    public static AssetLoadOperation<T> LoadRuntime<T>(T asset, AssetLoadPriority priority = AssetLoadPriority.Normal)
        where T : Asset;
    public static void Register<T>(AssetId id, T asset)
        where T : ScriptableObject;
    public static T Load<T>(T asset)
        where T : ScriptableObject;
    public static bool TryLoad<T>(T asset, out T? value)
        where T : ScriptableObject;
    public static ValueTask<T> LoadAsync<T>(T asset, CancellationToken cancellation = default)
        where T : ScriptableObject;
    public static bool Unload(AssetId id);
}
```

## Keire.Audio

Sources: [RuntimeApi.cs](../../KeireManaged/RuntimeApi.cs)

```csharp
public static class Audio
{
    public static float DecibelsToLinear(float decibels);
    public static float LinearToDecibels(float gain);
    public static bool Play(Entity entity);
    public static bool Play(Entity entity, AssetId clip, float volume = 1.0f);
    public static bool Play(Entity entity, AudioClip clip);
    public static bool Play(Entity entity, AudioClip clip, AudioPlaybackOptions options);
    public static bool Play(Entity entity, AssetId clip, AudioPlaybackOptions options);
    public static bool Stop(Entity entity);
    public static bool Pause(Entity entity);
    public static bool Resume(Entity entity);
    public static bool Seek(Entity entity, float time);
    public static AudioSourceStatus GetStatus(Entity entity);
}
```

## Keire.AudioClip

Sources: [RuntimeApi.cs](../../KeireManaged/RuntimeApi.cs)

```csharp
[StableAssetTypeId("4b454952-4541-5544-494f-434c49500001")]
public sealed class AudioClip : Asset
{
}
```

## Keire.AudioListener

Sources: [RuntimeApi.cs](../../KeireManaged/RuntimeApi.cs)

```csharp
[StableComponentId("4b454952-4541-5544-494f-4c4953540001")]
public sealed class AudioListener : Component
{
    public bool Primary { get; set; }
    public float Gain { get; set; }
    public float VolumeDecibels { get; set; }
}
```

## Keire.AudioMixer

Sources: [RuntimeApi.cs](../../KeireManaged/RuntimeApi.cs)

```csharp
[StableAssetTypeId("4b454952-4541-5544-4d49-584552303031")]
public sealed class AudioMixer : Asset
{
}
```

## Keire.AudioPlaybackOptions

Sources: [RuntimeApi.cs](../../KeireManaged/RuntimeApi.cs)

```csharp
public readonly record struct AudioPlaybackOptions
{
    public AudioPlaybackOptions();
    public string Bus { get; init; }
    public AudioMixer? Mixer { get; init; }
    public AssetId BusId { get; init; }
    public float Gain { get; init; }
    public float Pitch { get; init; }
    public uint Priority { get; init; }
    public bool Loop { get; init; }
    public bool Spatial { get; init; }
    public float MinimumDistance { get; init; }
    public float MaximumDistance { get; init; }
}
```

## Keire.AudioPlaybackState

Sources: [RuntimeApi.cs](../../KeireManaged/RuntimeApi.cs)

```csharp
public enum AudioPlaybackState : byte
{
    Stopped,
    Playing,
    Paused
}
```

## Keire.AudioReverbZone

Sources: [RuntimeApi.cs](../../KeireManaged/RuntimeApi.cs)

```csharp
[StableComponentId("4b454952-4541-5544-494f-52565a4f0001")]
public sealed class AudioReverbZone : Component
{
    public AudioMixer? Mixer { get; set; }
    public AssetId SnapshotId { get; set; }
    public AudioReverbZoneShape Shape { get; set; }
    public Vector3 BoxHalfExtent { get; set; }
    public float SphereRadius { get; set; }
    public int Priority { get; set; }
    public float BlendDistance { get; set; }
    public float ReverbSend { get; set; }
}
```

## Keire.AudioReverbZoneShape

Sources: [RuntimeApi.cs](../../KeireManaged/RuntimeApi.cs)

```csharp
public enum AudioReverbZoneShape : byte
{
    Box,
    Sphere
}
```

## Keire.AudioSource

Sources: [RuntimeApi.cs](../../KeireManaged/RuntimeApi.cs)

```csharp
[StableComponentId("4b454952-4541-5544-494f-535243000001")]
public sealed class AudioSource : Component
{
    public AudioClip? Clip { get; set; }
    public float Volume { get; set; }
    public float VolumeDecibels { get; set; }
    public AudioMixer? Mixer { get; set; }
    public AssetId BusId { get; set; }
    public float Pitch { get; set; }
    public bool Loop { get; set; }
    public bool Spatial { get; set; }
    public bool PlayOnAwake { get; set; }
    public uint Priority { get; set; }
    public float MinimumDistance { get; set; }
    public float MaximumDistance { get; set; }
    public AudioSourceStatus Status { get; }
    public AudioPlaybackState State { get; }
    public bool IsPlaying { get; }
    public bool IsPaused { get; }
    public float Time { get; set; }
    public float Duration { get; }
    public bool Play();
    public bool Play(AudioClip clip);
    public bool Play(AudioClip clip, AudioPlaybackOptions options);
    public bool Pause();
    public bool Resume();
    public bool Seek(float time);
    public bool Stop();
}
```

## Keire.AudioSourceStatus

Sources: [RuntimeApi.cs](../../KeireManaged/RuntimeApi.cs)

```csharp
public readonly record struct AudioSourceStatus(AudioPlaybackState State, float Time, float Duration)
{
    public bool IsPlaying { get; }
    public bool IsPaused { get; }
}
```

## Keire.AvatarMask

Sources: [NativeAssets.cs](../../KeireManaged/NativeAssets.cs)

```csharp
[StableAssetTypeId("4b454952-4541-5641-5441-524d41534b01")]
public sealed class AvatarMask : Asset
{
}
```

## Keire.AxisControl

Sources: [DirectInput.cs](../../KeireManaged/DirectInput.cs)

```csharp
public sealed class AxisControl : InputControl
{
    public float ReadValue();
}
```

## Keire.Behaviour

Sources: [Behaviour.cs](../../KeireManaged/Behaviour.cs)

```csharp
public abstract class Behaviour : Component
{
    public override bool Enabled { get; set; }
    public CancellationToken LifetimeToken { get; }
    protected Coroutine StartCoroutine(System.Collections.IEnumerator routine);
    protected bool StopCoroutine(Coroutine coroutine);
    protected void StopAllCoroutines();
    [NonSerialized, HideInInspector]
    public string RuntimeSerializedState;
    [NonSerialized, HideInInspector]
    public string RuntimeStateWarnings;
    [NonSerialized, HideInInspector]
    public uint RuntimeCallbackMask;
    protected virtual void Awake();
    protected virtual void OnEnable();
    protected virtual void Start();
    protected virtual void FixedUpdate();
    protected virtual void Update();
    protected virtual void LateUpdate();
    protected virtual void OnDisable();
    protected virtual void OnDestroy();
    protected virtual void OnCollisionEnter(CollisionContact contact);
    protected virtual void OnCollisionStay(CollisionContact contact);
    protected virtual void OnCollisionExit(CollisionContact contact);
    protected virtual void OnTriggerEnter(CollisionContact contact);
    protected virtual void OnTriggerStay(CollisionContact contact);
    protected virtual void OnTriggerExit(CollisionContact contact);
    protected virtual void OnAnimationEvent(AnimationEvent animationEvent);
    protected virtual void OnProceduralMotionEvent(ProceduralMotionEvent motionEvent);
    protected virtual void OnAnimatorIk(AnimationIkContext context);
    protected virtual void OnBeforeReload();
    protected virtual void OnAfterReload();
    protected virtual void OnValidate();
    public void RuntimeAttach(ulong world, ulong entityHigh, ulong entityLow);
    public uint RuntimeGetCallbackMask();
    public void RuntimeAwake();
    public void RuntimeEnable();
    public void RuntimeStart();
    public void RuntimeFixedUpdate(float deltaSeconds);
    public void RuntimeUpdate(float deltaSeconds);
    public void RuntimeLateUpdate();
    public void RuntimeAnimationEvent(string name, float normalizedTime, int integer, float scalar, string text);
    public void RuntimeAnimatorIk(float layerWeight);
    public void RuntimeAnimatorIkEvaluation(float layerWeight, float interpolationAlpha, byte isFixedUpdate);
    public void RuntimeProceduralMotionEvent(byte type, byte foot, byte state, float phase, float intensity, Vector3 contactPosition, Vector3 contactNormal, ulong supportHigh, ulong supportLow, ulong materialHigh, ulong materialLow);
    public void RuntimePhysicsContact(byte phase, byte trigger, ulong otherHigh, ulong otherLow, Vector3 point, Vector3 normal, float impulse);
    public void RuntimeDisable();
    public void RuntimeDestroy();
    public void RuntimeBeforeReload();
    public void RuntimeAfterReload();
    public void RuntimeResumeAfterFailedReload();
    public void RuntimeCaptureReloadState();
    public void RuntimeCapturePersistentState();
    public void RuntimeRestoreReloadState();
    public void RuntimeRestorePersistentState();
}
```

## Keire.BinaryAsset

Sources: [NativeAssets.cs](../../KeireManaged/NativeAssets.cs)

```csharp
[StableAssetTypeId("4b454952-4542-494e-4152-590000000001")]
public sealed class BinaryAsset : Asset
{
}
```

## Keire.ButtonControl

Sources: [DirectInput.cs](../../KeireManaged/DirectInput.cs)

```csharp
public sealed class ButtonControl : InputControl
{
    public bool IsPressed { get; }
    public bool WasPressedThisFrame { get; }
    public bool WasReleasedThisFrame { get; }
    public bool WasPressed { get; }
    public bool WasReleased { get; }
    public float ReadValue();
}
```

## Keire.Camera

Sources: [Rendering.cs](../../KeireManaged/Rendering.cs)

```csharp
[StableComponentId("4b454952-4543-414d-4552-410000000001")]
public sealed class Camera : Component
{
    public Material? EffectBeforeTonemapping { get; set; }
    public Material? EffectAfterTonemapping { get; set; }
    public Material? EffectAfterUi { get; set; }
    public CameraProjection Projection { get; set; }
    public CameraClearMode ClearMode { get; set; }
    public bool Primary { get; set; }
    public int Priority { get; set; }
    public float VerticalFieldOfView { get; set; }
    public float OrthographicSize { get; set; }
    public float NearPlane { get; set; }
    public float FarPlane { get; set; }
    public Color ClearColor { get; set; }
}
```

## Keire.CameraClearMode

Sources: [Rendering.cs](../../KeireManaged/Rendering.cs)

```csharp
public enum CameraClearMode
{
    Skybox,
    SolidColor
}
```

## Keire.CameraProjection

Sources: [Rendering.cs](../../KeireManaged/Rendering.cs)

```csharp
public enum CameraProjection
{
    Perspective,
    Orthographic
}
```

## Keire.CharacterController

Sources: [RuntimeApi.cs](../../KeireManaged/RuntimeApi.cs)

```csharp
[StableComponentId("4b454952-4543-4841-5241-435445520001")]
public sealed class CharacterController : Component
{
    public CharacterControllerState State { get; }
    public bool Grounded { get; }
    public Vector3 GroundNormal { get; }
    public Vector3 Velocity { get; }
    public bool Move(Vector3 displacement);
}
```

## Keire.CharacterControllerState

Sources: [RuntimeApi.cs](../../KeireManaged/RuntimeApi.cs)

```csharp
public readonly record struct CharacterControllerState(bool Grounded, Vector3 GroundNormal, Vector3 Velocity)
{
}
```

## Keire.Collider

Sources: [BuiltInComponents.cs](../../KeireManaged/BuiltInComponents.cs)

```csharp
[StableComponentId("4b454952-4543-4f4c-4c49-444552000001")]
public sealed class Collider : Component
{
    public ColliderShape Shape { get; set; }
    public Vector3 Center { get; set; }
    public Vector3 HalfExtent { get; set; }
    public float Radius { get; set; }
    public float Height { get; set; }
    public Mesh? CollisionMesh { get; set; }
    public PhysicsMaterial? Material { get; set; }
    public uint CollisionMask { get; set; }
    public bool IsTrigger { get; set; }
}
```

## Keire.ColliderShape

Sources: [BuiltInComponents.cs](../../KeireManaged/BuiltInComponents.cs)

```csharp
public enum ColliderShape : byte
{
    Box,
    Sphere,
    Capsule,
    ConvexMesh,
    TriangleMesh
}
```

## Keire.CollisionContact

Sources: [Behaviour.cs](../../KeireManaged/Behaviour.cs)

```csharp
public readonly record struct CollisionContact(Entity Other, Vector3 Point, Vector3 Normal, float Impulse, bool Trigger)
{
}
```

## Keire.Color

Sources: [MathTypes.cs](../../KeireManaged/MathTypes.cs)

```csharp
public readonly record struct Color(float Red, float Green, float Blue, float Alpha = 1.0f)
{
    public static Color White { get; }
    public static Color RedColor { get; }
    public static Color Lerp(Color from, Color to, float amount);
    public override string ToString();
}
```

## Keire.Component

Sources: [Handles.cs](../../KeireManaged/Handles.cs)

```csharp
public abstract class Component : EngineObject, IEquatable<Component>
{
    protected Component();
    protected Component(Entity entity);
    public Entity Entity { get; internal set; }
    public Transform Transform { get; }
    public override string Name { get; set; }
    public ComponentTypeId Type { get; }
    public override bool IsValid { get; }
    public virtual bool Enabled { get; set; }
    public bool IsActiveAndEnabled { get; }
    protected bool GetBuiltinBoolean(string key);
    protected long GetBuiltinInteger(string key);
    protected float GetBuiltinScalar(string key);
    protected Vector2 GetBuiltinVector2(string key);
    protected Vector3 GetBuiltinVector3(string key);
    protected Vector4 GetBuiltinVector4(string key);
    protected Color GetBuiltinColor(string key);
    protected string GetBuiltinText(string key);
    protected T? GetBuiltinAsset<T>(string key)
        where T : Asset;
    protected Entity? GetBuiltinEntity(string key);
    protected void SetBuiltinBoolean(string key, bool value);
    protected void SetBuiltinInteger(string key, long value);
    protected void SetBuiltinScalar(string key, float value);
    protected void SetBuiltinVector2(string key, Vector2 value);
    protected void SetBuiltinVector3(string key, Vector3 value);
    protected void SetBuiltinVector4(string key, Vector4 value);
    protected void SetBuiltinColor(string key, Color value);
    protected void SetBuiltinText(string key, string value);
    protected void SetBuiltinAsset(string key, Asset? value);
    protected void SetBuiltinEntity(string key, Entity? value);
    public T? GetComponent<T>()
        where T : class;
    public Component? GetComponent(Type type);
    public bool TryGetComponent<T>([NotNullWhen(true)] out T? component)
        where T : class;
    public bool TryGetComponent(Type type, [NotNullWhen(true)] out Component? component);
    public T[] GetComponents<T>()
        where T : class;
    public Component[] GetComponents(Type type);
    public void GetComponents<T>(List<T> results)
        where T : class;
    public void GetComponents(Type type, List<Component> results);
    public T? GetComponentInChildren<T>(bool includeInactive = false)
        where T : class;
    public Component? GetComponentInChildren(Type type, bool includeInactive = false);
    public T[] GetComponentsInChildren<T>(bool includeInactive = false)
        where T : class;
    public Component[] GetComponentsInChildren(Type type, bool includeInactive = false);
    public void GetComponentsInChildren<T>(List<T> results, bool includeInactive = false)
        where T : class;
    public void GetComponentsInChildren(Type type, List<Component> results, bool includeInactive = false);
    public T? GetComponentInParent<T>(bool includeInactive = false)
        where T : class;
    public Component? GetComponentInParent(Type type, bool includeInactive = false);
    public T[] GetComponentsInParent<T>(bool includeInactive = false)
        where T : class;
    public Component[] GetComponentsInParent(Type type, bool includeInactive = false);
    public void GetComponentsInParent<T>(List<T> results, bool includeInactive = false)
        where T : class;
    public void GetComponentsInParent(Type type, List<Component> results, bool includeInactive = false);
    public void Destroy();
    public bool Equals(Component? other);
    public override bool Equals(object? value);
    public override int GetHashCode();
}
```

## Keire.ComponentType

Sources: [Handles.cs](../../KeireManaged/Handles.cs)

```csharp
public static class ComponentType
{
    public static ComponentTypeId Of<T>();
    public static ComponentTypeId Of(Type type);
}
```

## Keire.ComponentTypeId

Sources: [Handles.cs](../../KeireManaged/Handles.cs)

```csharp
public readonly record struct ComponentTypeId(ulong High, ulong Low)
{
    public bool IsValid { get; }
}
```

## Keire.ComputeBackend

Sources: [Compute.cs](../../KeireManaged/Compute.cs)

```csharp
public enum ComputeBackend : byte
{
    D3D12,
    Vulkan,
    Metal
}
```

## Keire.ComputeBuffer

Sources: [Compute.cs](../../KeireManaged/Compute.cs)

```csharp
public sealed class ComputeBuffer : IDisposable
{
    public uint Size { get; }
    public bool Indirect { get; }
    public void Upload(ReadOnlySpan<byte> bytes, uint offset = 0);
    public ComputeSubmission RequestReadback(uint offset = 0, uint size = 0);
    public byte[] Readback(uint offset = 0, uint size = 0);
    public void Dispose();
}
```

## Keire.ComputeBufferBinding

Sources: [Compute.cs](../../KeireManaged/Compute.cs)

```csharp
public readonly record struct ComputeBufferBinding(uint Slot, ComputeBuffer Buffer, bool Writable = false)
{
}
```

## Keire.ComputeDevice

Sources: [Compute.cs](../../KeireManaged/Compute.cs)

```csharp
public sealed class ComputeDevice : IDisposable
{
    public ComputeDevice(ComputeBackend backend);
    public ComputeBuffer CreateBuffer(uint size, bool indirect = false);
    public ComputePipeline CreatePipeline(ulong programKey, uint variant = 0);
    public ComputeSubmission Dispatch(ComputePipeline pipeline, ReadOnlySpan<ComputeBufferBinding> bindings, uint x, uint y = 1, uint z = 1, ReadOnlySpan<byte> uniforms = default);
    public ComputeSubmission DispatchIndirect(ComputePipeline pipeline, ReadOnlySpan<ComputeBufferBinding> bindings, ComputeBuffer arguments, uint offset = 0, ReadOnlySpan<byte> uniforms = default);
    public void Dispose();
}
```

## Keire.ComputePipeline

Sources: [Compute.cs](../../KeireManaged/Compute.cs)

```csharp
public sealed class ComputePipeline : IDisposable
{
    public void Reload(ulong programKey, uint variant = 0);
    public void Dispose();
}
```

## Keire.ComputeSubmission

Sources: [Compute.cs](../../KeireManaged/Compute.cs)

```csharp
public sealed class ComputeSubmission : IDisposable
{
    public byte[] GetReadback();
    public bool IsComplete { get; }
    public void Wait();
    public void Dispose();
}
```

## Keire.Coroutine

Sources: [Coroutines.cs](../../KeireManaged/Coroutines.cs)

```csharp
public readonly record struct Coroutine
{
    public bool IsRunning { get; }
    public bool Stop();
}
```

## Keire.CreateAssetMenuAttribute

Sources: [SerializationAttributes.cs](../../KeireManaged/SerializationAttributes.cs)

```csharp
[AttributeUsage(AttributeTargets.Class, Inherited = false)]
public sealed class CreateAssetMenuAttribute(string menuName, string fileName = "") : Attribute
{
    public string MenuName { get; }
    public string FileName { get; }
}
```

## Keire.Cursor

Sources: [RuntimeApi.cs](../../KeireManaged/RuntimeApi.cs)

```csharp
public static class Cursor
{
    public static bool Visible { get; }
    public static bool Locked { get; }
    public static bool VisibilityRequested { get; }
    public static IDisposable RequestCapture();
    public static IDisposable RequestVisible();
    public static void Hide();
    public static void Show();
    public static void Lock();
    public static void Unlock();
}
```

## Keire.CustomManagedValueConverterAttribute

Sources: [ManagedCustomSerialization.cs](../../KeireManaged/ManagedCustomSerialization.cs)

```csharp
[AttributeUsage(AttributeTargets.Class, Inherited = false)]
public sealed class CustomManagedValueConverterAttribute : Attribute
{
    public CustomManagedValueConverterAttribute(Type targetType, string stableId, uint version);
    public Type TargetType { get; }
    public Guid StableId { get; }
    public uint Version { get; }
}
```

## Keire.CustomYieldInstruction

Sources: [Coroutines.cs](../../KeireManaged/Coroutines.cs)

```csharp
public abstract class CustomYieldInstruction : YieldInstruction
{
    public abstract bool KeepWaiting { get; }
}
```

## Keire.Debug

Sources: [Debug.cs](../../KeireManaged/Debug.cs)

```csharp
public static class Debug
{
    public static void Log(object? message);
    public static void Warn(object? message);
    public static void LogWarning(object? message);
    public static void Error(object? message);
    public static void LogError(object? message);
    public static void Log(string? format, params object? []? args);
    public static void Warn(string? format, params object? []? args);
    public static void LogWarning(string? format, params object? []? args);
    public static void Error(string? format, params object? []? args);
    public static void LogError(string? format, params object? []? args);
    public static void LogFormat(string format, params object? [] args);
    public static void LogWarningFormat(string format, params object? [] args);
    public static void LogErrorFormat(string format, params object? [] args);
    public static void Log(object? first, object? second, params object? []? rest);
    public static void Warn(object? first, object? second, params object? []? rest);
    public static void LogWarning(object? first, object? second, params object? []? rest);
    public static void Error(object? first, object? second, params object? []? rest);
    public static void LogError(object? first, object? second, params object? []? rest);
    public static void LogException(Exception exception);
    public static void Assert(bool condition, object? message = null);
    public static void DrawLine(Vector3 start, Vector3 end, Color color, float duration = 0.0f);
}
```

## Keire.DirectionalLight

Sources: [Rendering.cs](../../KeireManaged/Rendering.cs)

```csharp
[StableComponentId("4b454952-4544-4952-4c49-474854000001")]
public sealed class DirectionalLight : Component
{
    public Color Color { get; set; }
    public float Intensity { get; set; }
    public ShadowQuality Shadows { get; set; }
    public float ShadowStrength { get; set; }
    public float ShadowBias { get; set; }
    public LightBakeMode BakeMode { get; set; }
    public ShadowResolution ShadowResolution { get; set; }
    public Texture? Cookie { get; set; }
    public bool ContactShadows { get; set; }
    public float IndirectMultiplier { get; set; }
    public bool UseColorTemperature { get; set; }
    public float ColorTemperature { get; set; }
    public Vector2 CookieScale { get; set; }
    public Vector2 CookieOffset { get; set; }
    public float CookieRotation { get; set; }
}
```

## Keire.DistanceJoint

Sources: [BuiltInComponents.cs](../../KeireManaged/BuiltInComponents.cs)

```csharp
[StableComponentId("4b454952-4544-4953-544a-4f494e540001")]
public sealed class DistanceJoint : Joint
{
    public float MinimumDistance { get; set; }
    public float MaximumDistance { get; set; }
}
```

## Keire.DynamicMaterial

Sources: [Rendering.cs](../../KeireManaged/Rendering.cs)

```csharp
public sealed class DynamicMaterial
{
    public Entity Entity { get; }
    public uint MaterialSlot { get; }
    public Material? SharedMaterial { get; }
    public void SetFloat(string name, float value);
    public void SetVector(string name, Vector2 value);
    public void SetVector(string name, Vector3 value);
    public void SetVector(string name, Vector4 value);
    public void SetColor(string name, Color value);
    public void SetTexture(string name, Texture? value);
    public bool Reset(string name);
    public void Clear();
}
```

## Keire.EngineObject

Sources: [Handles.cs](../../KeireManaged/Handles.cs)

```csharp
public abstract class EngineObject
{
    public abstract bool IsValid { get; }
    public virtual string Name { get; set; }
    public static Entity Instantiate(Prefab prefab);
    public static Entity Instantiate(Prefab prefab, Vector3 position, Quaternion rotation);
    public static Entity Instantiate(Prefab prefab, Vector3 position, Quaternion rotation, Entity? parent);
    public static Entity Instantiate(Prefab prefab, Vector3 position, Quaternion rotation, Entity? parent, bool active);
    public static Entity Instantiate(Entity original);
    public static void Destroy(EngineObject value);
    public static void Destroy(Entity entity, float delaySeconds);
    public static bool DontDestroyOnLoad(Entity entity);
}
```

## Keire.Entity

Sources: [Handles.cs](../../KeireManaged/Handles.cs)

```csharp
public sealed class Entity : EngineObject, IEquatable<Entity>
{
    public ulong World { get; }
    public EntityId Id { get; }
    public override bool IsValid { get; }
    public override string Name { get; set; }
    public bool Active { get; set; }
    public bool ActiveInHierarchy { get; }
    public uint Layer { get; set; }
    public IReadOnlyList<string> Tags { get; }
    public Entity? Parent { get; set; }
    public IReadOnlyList<Entity> Children { get; }
    public Transform Transform { get; }
    public bool HasTag(string tag);
    public bool AddTag(string tag);
    public bool RemoveTag(string tag);
    public void ClearTags();
    public Component? GetComponent(Type type);
    public T? GetComponent<T>()
        where T : class;
    public bool TryGetComponent<T>([NotNullWhen(true)] out T? component)
        where T : class;
    public bool TryGetComponent(Type type, [NotNullWhen(true)] out Component? component);
    public Component[] GetComponents(Type type);
    public T[] GetComponents<T>()
        where T : class;
    public void GetComponents(Type type, List<Component> results);
    public void GetComponents<T>(List<T> results)
        where T : class;
    public T? GetComponentInChildren<T>(bool includeInactive = false)
        where T : class;
    public Component? GetComponentInChildren(Type type, bool includeInactive = false);
    public T[] GetComponentsInChildren<T>(bool includeInactive = false)
        where T : class;
    public Component[] GetComponentsInChildren(Type type, bool includeInactive = false);
    public void GetComponentsInChildren(Type type, List<Component> results, bool includeInactive = false);
    public void GetComponentsInChildren<T>(List<T> results, bool includeInactive = false)
        where T : class;
    public T? GetComponentInParent<T>(bool includeInactive = false)
        where T : class;
    public Component? GetComponentInParent(Type type, bool includeInactive = false);
    public T[] GetComponentsInParent<T>(bool includeInactive = false)
        where T : class;
    public Component[] GetComponentsInParent(Type type, bool includeInactive = false);
    public void GetComponentsInParent(Type type, List<Component> results, bool includeInactive = false);
    public void GetComponentsInParent<T>(List<T> results, bool includeInactive = false)
        where T : class;
    public Component AddComponent(Type type);
    public T AddComponent<T>()
        where T : Component;
    public bool HasComponent<T>()
        where T : class;
    public bool HasComponent(ComponentTypeId type);
    public bool RemoveComponent<T>()
        where T : Component;
    public bool RemoveComponent(Type type);
    public void SetParent(Entity? parent, bool preserveWorldTransform = true);
    public Entity? FindChild(string name, bool recursive = false);
    public Entity? Find(string path);
    public Entity Instantiate();
    public void Destroy();
    public void Destroy(float delaySeconds);
    public bool Equals(Entity? other);
    public override bool Equals(object? value);
    public override int GetHashCode();
    public static bool operator ==(Entity? left, Entity? right);
    public static bool operator !=(Entity? left, Entity? right);
}
```

## Keire.EntityId

Sources: [Handles.cs](../../KeireManaged/Handles.cs)

```csharp
public readonly record struct EntityId(ulong High, ulong Low)
{
    public bool IsValid { get; }
}
```

## Keire.ExecutionOrderAttribute

Sources: [SerializationAttributes.cs](../../KeireManaged/SerializationAttributes.cs)

```csharp
[AttributeUsage(AttributeTargets.Class, Inherited = true)]
public sealed class ExecutionOrderAttribute : Attribute
{
    public ExecutionOrderAttribute(int order);
    public readonly int Order;
}
```

## Keire.FixedJoint

Sources: [BuiltInComponents.cs](../../KeireManaged/BuiltInComponents.cs)

```csharp
[StableComponentId("4b454952-4546-4958-4544-4a4f494e5401")]
public sealed class FixedJoint : Joint
{
}
```

## Keire.ForceMode

Sources: [RuntimeApi.cs](../../KeireManaged/RuntimeApi.cs)

```csharp
public enum ForceMode : byte
{
    Force,
    Acceleration,
    Impulse,
    VelocityChange
}
```

## Keire.FormerlySerializedAsAttribute

Sources: [SerializationAttributes.cs](../../KeireManaged/SerializationAttributes.cs)

```csharp
[AttributeUsage(AttributeTargets.Field | AttributeTargets.Property, AllowMultiple = true)]
public sealed class FormerlySerializedAsAttribute(string name) : Attribute
{
    public string Name { get; }
    public readonly string SerializedName;
}
```

## Keire.FullscreenMode

Sources: [RuntimeFoundation.cs](../../KeireManaged/RuntimeFoundation.cs)

```csharp
public enum FullscreenMode : byte
{
    Windowed,
    BorderlessFullscreen
}
```

## Keire.GIReceiveMode

Sources: [Rendering.cs](../../KeireManaged/Rendering.cs)

```csharp
public enum GIReceiveMode
{
    LightProbes,
    Lightmaps,
    Disabled
}
```

## Keire.Gamepad

Sources: [DirectInput.cs](../../KeireManaged/DirectInput.cs)

```csharp
public sealed class Gamepad
{
    public uint DeviceId { get; }
    public static Gamepad? Current { get; }
    public static IReadOnlyList<Gamepad> All { get; }
    public Vector2Control LeftStick { get; }
    public Vector2Control RightStick { get; }
    public Vector2Control Dpad { get; }
    public AxisControl LeftTrigger { get; }
    public AxisControl RightTrigger { get; }
    public ButtonControl ButtonSouth { get; }
    public ButtonControl ButtonEast { get; }
    public ButtonControl ButtonWest { get; }
    public ButtonControl ButtonNorth { get; }
    public ButtonControl LeftShoulder { get; }
    public ButtonControl RightShoulder { get; }
    public ButtonControl LeftStickButton { get; }
    public ButtonControl RightStickButton { get; }
    public ButtonControl StartButton { get; }
    public ButtonControl SelectButton { get; }
    public Vector2Control leftStick { get; }
    public Vector2Control rightStick { get; }
    public ButtonControl buttonSouth { get; }
    public ButtonControl buttonEast { get; }
    public bool SetMotorSpeeds(float lowFrequency, float highFrequency, float durationSeconds = 0.25f);
}
```

## Keire.GlobalMaterialParameters

Sources: [Rendering.cs](../../KeireManaged/Rendering.cs)

```csharp
public static class GlobalMaterialParameters
{
    public static MaterialParameterCollectionInstance Open(MaterialParameterCollection collection);
}
```

## Keire.HeaderAttribute

Sources: [SerializationAttributes.cs](../../KeireManaged/SerializationAttributes.cs)

```csharp
[AttributeUsage(AttributeTargets.Field | AttributeTargets.Property)]
public sealed class HeaderAttribute : Attribute
{
    public HeaderAttribute(string text);
    public string Text { get; }
    public readonly string Value;
}
```

## Keire.HideInInspectorAttribute

Sources: [SerializationAttributes.cs](../../KeireManaged/SerializationAttributes.cs)

```csharp
[AttributeUsage(AttributeTargets.Field | AttributeTargets.Property)]
public sealed class HideInInspectorAttribute : Attribute
{
}
```

## Keire.HingeJoint

Sources: [BuiltInComponents.cs](../../KeireManaged/BuiltInComponents.cs)

```csharp
[StableComponentId("4b454952-4548-494e-4745-4a4f494e5401")]
public sealed class HingeJoint : Joint
{
    public Vector3 Axis { get; set; }
    public bool LimitsEnabled { get; set; }
    public float LowerLimit { get; set; }
    public float UpperLimit { get; set; }
    public bool MotorEnabled { get; set; }
    public float MotorSpeed { get; set; }
    public float MaximumMotorTorque { get; set; }
}
```

## Keire.HotReloadStateAttribute

Sources: [SerializationAttributes.cs](../../KeireManaged/SerializationAttributes.cs)

```csharp
[AttributeUsage(AttributeTargets.Field)]
public sealed class HotReloadStateAttribute : Attribute
{
}
```

## Keire.IManagedValueMigration

Sources: [ManagedCustomSerialization.cs](../../KeireManaged/ManagedCustomSerialization.cs)

```csharp
public interface IManagedValueMigration
{
    ManagedSerializedValue Migrate(ManagedSerializedValue value, ManagedMigrationContext context);
}
```

## Keire.IRuntimeBridge

Sources: [RuntimeApi.cs](../../KeireManaged/RuntimeApi.cs)

```csharp
public interface IRuntimeBridge
{
    bool EntityExists(Entity entity);
    string GetEntityName(Entity entity);
    void SetEntityName(Entity entity, string name);
    bool GetEntityActive(Entity entity);
    void SetEntityActive(Entity entity, bool active);
    Entity? GetEntityParent(Entity entity);
    void SetEntityParent(Entity entity, Entity? parent);
    IReadOnlyList<Entity> GetEntityChildren(Entity entity);
    bool RemoveComponent(Entity entity, ComponentTypeId type);
    Entity CloneEntity(Entity entity);
    void DestroyEntity(Entity entity);
    Vector3 GetLocalPosition(Entity entity);
    void SetLocalPosition(Entity entity, Vector3 value);
    Quaternion GetLocalRotation(Entity entity);
    void SetLocalRotation(Entity entity, Quaternion value);
    Vector3 GetLocalScale(Entity entity);
    void SetLocalScale(Entity entity, Vector3 value);
    float DeltaTime { get; }
    float FixedDeltaTime { get; }
    float UnscaledDeltaTime { get; }
    double ElapsedTime { get; }
    bool GetInputButton(string action);
    float GetInputAxis(string action);
    IReadOnlyList<RaycastHit> Raycast(Vector3 origin, Vector3 direction, float maximumDistance, uint mask);
    ValueTask<NavigationPath> FindPathAsync(Vector3 start, Vector3 end, uint areaMask, CancellationToken cancellation);
    void SetAnimatorFloat(Entity entity, string parameter, float value);
    void SetAnimatorBool(Entity entity, string parameter, bool value);
    void SetAnimatorTrigger(Entity entity, string parameter);
    void PlayAudio(Entity entity, AssetId clip, float volume);
    void StopAudio(Entity entity);
    void DrawLine(Vector3 start, Vector3 end, Color color, float duration);
    void WriteLog(LogLevel level, string message);
}
```

## Keire.IRuntimeService

Sources: [RuntimeServices.cs](../../KeireManaged/RuntimeServices.cs)

```csharp
public interface IRuntimeService
{
    void Start(RuntimeServiceContext context);
    void Update(RuntimeServiceUpdateContext context);
    void Stop();
}
```

## Keire.IRuntimeServiceHotReloadState

Sources: [RuntimeServices.cs](../../KeireManaged/RuntimeServices.cs)

```csharp
public interface IRuntimeServiceHotReloadState
{
    ManagedSerializedValue CaptureState();
    void RestoreState(ManagedSerializedValue state);
}
```

## Keire.ISerializationCallbackReceiver

Sources: [ManagedCustomSerialization.cs](../../KeireManaged/ManagedCustomSerialization.cs)

```csharp
public interface ISerializationCallbackReceiver
{
    void OnBeforeSerialize();
    void OnAfterDeserialize();
}
```

## Keire.Input

Sources: [DirectInput.cs](../../KeireManaged/DirectInput.cs), [RuntimeApi.cs](../../KeireManaged/RuntimeApi.cs)

```csharp
public static partial class Input
{
}
public static partial class Input
{
    public static IReadOnlyList<InputDevice> Devices { get; }
    public static string ControlScheme { get; }
    [Obsolete("Use an InputActionAsset, InputActionContext, or direct device controls instead.")]
    public static Vector2 Axis2D(string action);
    [Obsolete("Use InputAction.ReadValueAsButton or a ButtonControl instead.")]
    public static bool Held(string action);
    [Obsolete("Use InputAction.WasPressedThisFrame or a ButtonControl instead.")]
    public static bool Pressed(string action);
    [Obsolete("Use InputAction.WasReleasedThisFrame or a ButtonControl instead.")]
    public static bool Released(string action);
    [Obsolete("Use InputAction.ReadValueAsButton or a ButtonControl instead.")]
    public static bool Button(string action);
    [Obsolete("Use InputAction.ReadValue<float> or an AxisControl instead.")]
    public static float Axis(string action);
    public static bool TrySetControlScheme(string scheme, bool locked = true);
    public static bool ClearControlSchemeLock();
    public static bool TrySetGamepadRumble(uint device, float lowFrequency, float highFrequency, float durationSeconds);
    public static InputRebindOperation BeginInteractiveRebind(AssetId binding);
    public static InputRebindOperation BeginInteractiveRebind(AssetId binding, InputRebindOptions options);
    public static bool SaveBindingOverrides(string profile);
    public static int LoadBindingOverrides(string profile);
    public static bool ClearBindingOverrides();
}
```

## Keire.Input.Gamepad

Sources: [DirectInput.cs](../../KeireManaged/DirectInput.cs)

```csharp
public static class Gamepad
{
    public static Keire.Gamepad? Current { get; }
    public static IReadOnlyList<Keire.Gamepad> All { get; }
}
```

## Keire.Input.Keyboard

Sources: [DirectInput.cs](../../KeireManaged/DirectInput.cs)

```csharp
public static class Keyboard
{
    public static Keire.Keyboard? Current { get; }
}
```

## Keire.Input.Mouse

Sources: [DirectInput.cs](../../KeireManaged/DirectInput.cs)

```csharp
public static class Mouse
{
    public static Keire.Mouse? Current { get; }
}
```

## Keire.InputAction

Sources: [InputActions.cs](../../KeireManaged/InputActions.cs)

```csharp
public sealed class InputAction
{
    public AssetId Id { get; }
    public string Name { get; }
    public InputActionMap ActionMap { get; }
    public InputActionPhase Phase { get; }
    public bool Enabled { get; }
    public bool IsPressed { get; }
    public bool WasPressedThisFrame { get; }
    public bool WasPerformedThisFrame { get; }
    public bool WasReleasedThisFrame { get; }
    public event Action<CallbackContext> started { add; remove; }
    public event Action<CallbackContext> performed { add; remove; }
    public event Action<CallbackContext> canceled { add; remove; }
    public void Enable();
    public void Disable();
    public InputRebindOperation BeginInteractiveRebind(AssetId binding);
    public InputRebindOperation BeginInteractiveRebind(AssetId binding, InputRebindOptions options);
    public bool ReadValueAsButton();
    public T ReadValue<T>()
        where T : struct;
}
```

## Keire.InputAction.CallbackContext

Sources: [InputActions.cs](../../KeireManaged/InputActions.cs)

```csharp
public readonly struct CallbackContext
{
    public InputAction Action { get; }
    public InputActionPhase Phase { get; }
    public bool Started { get; }
    public bool Performed { get; }
    public bool Canceled { get; }
    public T ReadValue<T>()
        where T : struct;
}
```

## Keire.InputActionAsset

Sources: [InputActions.cs](../../KeireManaged/InputActions.cs), [NativeAssets.cs](../../KeireManaged/NativeAssets.cs)

```csharp
public sealed partial class InputActionAsset
{
    public InputActionContext CreateContext();
    public void Enable();
    public void Disable();
    public InputActionMap? FindActionMap(string name);
    public InputAction? FindAction(string path);
}
[StableAssetTypeId("4b454952-4549-4e50-5554-414354494f01")]
public sealed partial class InputActionAsset : Asset
{
}
```

## Keire.InputActionContext

Sources: [InputActions.cs](../../KeireManaged/InputActions.cs)

```csharp
public sealed class InputActionContext : IDisposable
{
    public InputActionAsset Asset { get; }
    public bool IsDisposed { get; }
    public void Enable();
    public void Disable();
    public InputActionMap? FindActionMap(string name);
    public InputAction? FindAction(string path);
    public InputActionMap GetActionMap(AssetId id, string name = "");
    public InputAction GetAction(AssetId map, AssetId action, string name = "");
    public void Dispose();
}
```

## Keire.InputActionMap

Sources: [InputActions.cs](../../KeireManaged/InputActions.cs)

```csharp
public sealed class InputActionMap
{
    public AssetId Id { get; }
    public string Name { get; }
    public void Enable();
    public void Disable();
    public InputAction? FindAction(string name);
    public InputAction GetAction(AssetId id, string name = "");
}
```

## Keire.InputActionPhase

Sources: [InputActions.cs](../../KeireManaged/InputActions.cs)

```csharp
public enum InputActionPhase : byte
{
    Disabled,
    Waiting,
    Started,
    Performed,
    Canceled
}
```

## Keire.InputControl

Sources: [DirectInput.cs](../../KeireManaged/DirectInput.cs)

```csharp
public abstract class InputControl
{
    protected InputControl(uint device, string path);
    public uint DeviceId { get; }
    public string Path { get; }
}
```

## Keire.InputDevice

Sources: [RuntimeApi.cs](../../KeireManaged/RuntimeApi.cs)

```csharp
public readonly record struct InputDevice(uint Id, InputDeviceType Type, string Name, bool Connected, bool Paired)
{
}
```

## Keire.InputDeviceMask

Sources: [RuntimeApi.cs](../../KeireManaged/RuntimeApi.cs)

```csharp
[Flags]
public enum InputDeviceMask : byte
{
    None = 0,
    Keyboard = 1 << 0,
    Mouse = 1 << 1,
    Gamepad = 1 << 2,
    All = Keyboard | Mouse | Gamepad
}
```

## Keire.InputDeviceType

Sources: [RuntimeApi.cs](../../KeireManaged/RuntimeApi.cs)

```csharp
public enum InputDeviceType : byte
{
    Keyboard,
    Mouse,
    Gamepad
}
```

## Keire.InputRebindOperation

Sources: [RuntimeApi.cs](../../KeireManaged/RuntimeApi.cs)

```csharp
public readonly struct InputRebindOperation
{
    public bool IsValid { get; }
    public InputRebindSnapshot Snapshot { get; }
    public bool Apply(InputRebindResolution resolution);
    public bool Cancel();
}
```

## Keire.InputRebindOptions

Sources: [RuntimeApi.cs](../../KeireManaged/RuntimeApi.cs)

```csharp
public readonly record struct InputRebindOptions(float MagnitudeThreshold, double TimeoutSeconds, InputDeviceMask AllowedDevices)
{
    public static InputRebindOptions Default { get; }
}
```

## Keire.InputRebindResolution

Sources: [RuntimeApi.cs](../../KeireManaged/RuntimeApi.cs)

```csharp
public enum InputRebindResolution : byte
{
    Replace,
    KeepBoth,
    Cancel
}
```

## Keire.InputRebindSnapshot

Sources: [RuntimeApi.cs](../../KeireManaged/RuntimeApi.cs)

```csharp
public readonly record struct InputRebindSnapshot(AssetId Binding, InputRebindStatus Status, string CandidatePath, double RemainingSeconds, uint ConflictCount)
{
}
```

## Keire.InputRebindStatus

Sources: [RuntimeApi.cs](../../KeireManaged/RuntimeApi.cs)

```csharp
public enum InputRebindStatus : byte
{
    Listening,
    Candidate,
    Completed,
    Cancelled,
    TimedOut
}
```

## Keire.InputValueType

Sources: [InputActions.cs](../../KeireManaged/InputActions.cs)

```csharp
public enum InputValueType : byte
{
    Boolean,
    Axis1D,
    Axis2D
}
```

## Keire.InspectorGroupAttribute

Sources: [SerializationAttributes.cs](../../KeireManaged/SerializationAttributes.cs)

```csharp
[AttributeUsage(AttributeTargets.Field | AttributeTargets.Property)]
public sealed class InspectorGroupAttribute : Attribute
{
    public InspectorGroupAttribute(string name);
    public readonly string Name;
}
```

## Keire.InspectorNameAttribute

Sources: [SerializationAttributes.cs](../../KeireManaged/SerializationAttributes.cs)

```csharp
[AttributeUsage(AttributeTargets.Field | AttributeTargets.Property)]
public sealed class InspectorNameAttribute : Attribute
{
    public InspectorNameAttribute(string name);
    public readonly string Name;
}
```

## Keire.InspectorStepAttribute

Sources: [SerializationAttributes.cs](../../KeireManaged/SerializationAttributes.cs)

```csharp
[AttributeUsage(AttributeTargets.Field | AttributeTargets.Property)]
public sealed class InspectorStepAttribute : Attribute
{
    public InspectorStepAttribute(double step);
    public readonly double Step;
}
```

## Keire.Job

Sources: [Jobs.cs](../../KeireManaged/Jobs.cs)

```csharp
public sealed class Job : IEquatable<Job>
{
    public ulong Id { get; }
    public bool IsValid { get; }
    public JobStatus Status { get; }
    public Task Completion { get; }
    public void Cancel();
    public bool Equals(Job? other);
    public override bool Equals(object? value);
    public override int GetHashCode();
}
```

## Keire.JobClass

Sources: [Jobs.cs](../../KeireManaged/Jobs.cs)

```csharp
public enum JobClass : byte
{
    Compute,
    Blocking
}
```

## Keire.JobContext

Sources: [Jobs.cs](../../KeireManaged/Jobs.cs)

```csharp
public sealed class JobContext
{
    public CancellationToken CancellationToken { get; }
    public bool IsCancellationRequested { get; }
}
```

## Keire.JobDescription

Sources: [Jobs.cs](../../KeireManaged/Jobs.cs)

```csharp
public sealed class JobDescription
{
    public string Name { get; init; }
    public JobPriority Priority { get; init; }
    public JobClass Class { get; init; }
    public IReadOnlyList<Job> Dependencies { get; init; }
}
```

## Keire.JobPriority

Sources: [Jobs.cs](../../KeireManaged/Jobs.cs)

```csharp
public enum JobPriority : byte
{
    Critical,
    High,
    Normal,
    Low,
    Background
}
```

## Keire.JobStatus

Sources: [Jobs.cs](../../KeireManaged/Jobs.cs)

```csharp
public enum JobStatus : byte
{
    Waiting,
    Running,
    Succeeded,
    Failed,
    Cancelled
}
```

## Keire.Jobs

Sources: [Jobs.cs](../../KeireManaged/Jobs.cs)

```csharp
public static unsafe class Jobs
{
    public static Job Submit(Action<JobContext> callback, JobDescription? description = null);
    public static Job Run(Action callback, JobDescription? description = null);
}
```

## Keire.Joint

Sources: [BuiltInComponents.cs](../../KeireManaged/BuiltInComponents.cs)

```csharp
public abstract class Joint : Component
{
    public Entity? ConnectedEntity { get; set; }
    public Vector3 LocalAnchor { get; set; }
    public Vector3 ConnectedAnchor { get; set; }
    public float BreakForce { get; set; }
    public float BreakTorque { get; set; }
    public bool EnableCollision { get; set; }
}
```

## Keire.KeireEvent

Sources: [Events.cs](../../KeireManaged/Events.cs)

```csharp
[SerializableType]
public sealed class KeireEvent : KeireEventBase
{
    public void AddListener(Action listener);
    public void RemoveListener(Action listener);
    public void RemoveAllListeners();
    public void Invoke();
}
```

## Keire.KeireEvent of T0 and T1 and T2 and T3

Sources: [Events.cs](../../KeireManaged/Events.cs)

```csharp
[SerializableType]
public sealed class KeireEvent<T0, T1, T2, T3> : KeireEventBase
{
    public void AddListener(Action<T0, T1, T2, T3> listener);
    public void RemoveListener(Action<T0, T1, T2, T3> listener);
    public void RemoveAllListeners();
    public void Invoke(T0 value0, T1 value1, T2 value2, T3 value3);
}
```

## Keire.KeireEvent of T0 and T1 and T2

Sources: [Events.cs](../../KeireManaged/Events.cs)

```csharp
[SerializableType]
public sealed class KeireEvent<T0, T1, T2> : KeireEventBase
{
    public void AddListener(Action<T0, T1, T2> listener);
    public void RemoveListener(Action<T0, T1, T2> listener);
    public void RemoveAllListeners();
    public void Invoke(T0 value0, T1 value1, T2 value2);
}
```

## Keire.KeireEvent of T0 and T1

Sources: [Events.cs](../../KeireManaged/Events.cs)

```csharp
[SerializableType]
public sealed class KeireEvent<T0, T1> : KeireEventBase
{
    public void AddListener(Action<T0, T1> listener);
    public void RemoveListener(Action<T0, T1> listener);
    public void RemoveAllListeners();
    public void Invoke(T0 value0, T1 value1);
}
```

## Keire.KeireEvent of T0

Sources: [Events.cs](../../KeireManaged/Events.cs)

```csharp
[SerializableType]
public sealed class KeireEvent<T0> : KeireEventBase
{
    public void AddListener(Action<T0> listener);
    public void RemoveListener(Action<T0> listener);
    public void RemoveAllListeners();
    public void Invoke(T0 value0);
}
```

## Keire.KeireEventBase

Sources: [Events.cs](../../KeireManaged/Events.cs)

```csharp
[SerializableType]
public abstract class KeireEventBase
{
    [SerializeField]
    protected List<PersistentEventCall> persistentCalls;
    public int PersistentListenerCount { get; }
    protected bool HasPersistentListeners { get; }
    protected void InvokePersistent(object? [] arguments);
}
```

## Keire.Key

Sources: [DirectInput.cs](../../KeireManaged/DirectInput.cs)

```csharp
public enum Key
{
    A,
    B,
    C,
    D,
    E,
    F,
    G,
    H,
    I,
    J,
    K,
    L,
    M,
    N,
    O,
    P,
    Q,
    R,
    S,
    T,
    U,
    V,
    W,
    X,
    Y,
    Z,
    Digit0,
    Digit1,
    Digit2,
    Digit3,
    Digit4,
    Digit5,
    Digit6,
    Digit7,
    Digit8,
    Digit9,
    Space,
    Enter,
    Escape,
    Tab,
    Backspace,
    UpArrow,
    DownArrow,
    LeftArrow,
    RightArrow,
    LeftShift,
    RightShift,
    LeftCtrl,
    RightCtrl,
    LeftAlt,
    RightAlt,
    LeftMeta,
    RightMeta,
    Insert,
    Delete,
    Home,
    End,
    PageUp,
    PageDown,
    CapsLock,
    PrintScreen,
    ScrollLock,
    Pause,
    Minus,
    Equals,
    LeftBracket,
    RightBracket,
    Backslash,
    Semicolon,
    Quote,
    Backquote,
    Comma,
    Period,
    Slash,
    F1,
    F2,
    F3,
    F4,
    F5,
    F6,
    F7,
    F8,
    F9,
    F10,
    F11,
    F12
}
```

## Keire.Keyboard

Sources: [DirectInput.cs](../../KeireManaged/DirectInput.cs)

```csharp
public sealed class Keyboard
{
    public uint DeviceId { get; }
    public static Keyboard? Current { get; }
    public ButtonControl this[Key key] { get; }
    public ButtonControl aKey { get; }
    public ButtonControl bKey { get; }
    public ButtonControl cKey { get; }
    public ButtonControl dKey { get; }
    public ButtonControl eKey { get; }
    public ButtonControl fKey { get; }
    public ButtonControl gKey { get; }
    public ButtonControl hKey { get; }
    public ButtonControl iKey { get; }
    public ButtonControl jKey { get; }
    public ButtonControl kKey { get; }
    public ButtonControl lKey { get; }
    public ButtonControl mKey { get; }
    public ButtonControl nKey { get; }
    public ButtonControl oKey { get; }
    public ButtonControl pKey { get; }
    public ButtonControl qKey { get; }
    public ButtonControl rKey { get; }
    public ButtonControl sKey { get; }
    public ButtonControl tKey { get; }
    public ButtonControl uKey { get; }
    public ButtonControl vKey { get; }
    public ButtonControl wKey { get; }
    public ButtonControl xKey { get; }
    public ButtonControl yKey { get; }
    public ButtonControl zKey { get; }
    public ButtonControl digit0Key { get; }
    public ButtonControl digit1Key { get; }
    public ButtonControl digit2Key { get; }
    public ButtonControl digit3Key { get; }
    public ButtonControl digit4Key { get; }
    public ButtonControl digit5Key { get; }
    public ButtonControl digit6Key { get; }
    public ButtonControl digit7Key { get; }
    public ButtonControl digit8Key { get; }
    public ButtonControl digit9Key { get; }
    public ButtonControl spaceKey { get; }
    public ButtonControl enterKey { get; }
    public ButtonControl escapeKey { get; }
    public ButtonControl tabKey { get; }
    public ButtonControl backspaceKey { get; }
    public ButtonControl upArrowKey { get; }
    public ButtonControl downArrowKey { get; }
    public ButtonControl leftArrowKey { get; }
    public ButtonControl rightArrowKey { get; }
    public ButtonControl leftShiftKey { get; }
    public ButtonControl rightShiftKey { get; }
    public ButtonControl leftCtrlKey { get; }
    public ButtonControl rightCtrlKey { get; }
    public ButtonControl leftAltKey { get; }
    public ButtonControl rightAltKey { get; }
    public ButtonControl leftMetaKey { get; }
    public ButtonControl rightMetaKey { get; }
    public ButtonControl insertKey { get; }
    public ButtonControl deleteKey { get; }
    public ButtonControl homeKey { get; }
    public ButtonControl endKey { get; }
    public ButtonControl pageUpKey { get; }
    public ButtonControl pageDownKey { get; }
    public ButtonControl capsLockKey { get; }
    public ButtonControl printScreenKey { get; }
    public ButtonControl scrollLockKey { get; }
    public ButtonControl pauseKey { get; }
    public ButtonControl minusKey { get; }
    public ButtonControl equalsKey { get; }
    public ButtonControl leftBracketKey { get; }
    public ButtonControl rightBracketKey { get; }
    public ButtonControl backslashKey { get; }
    public ButtonControl semicolonKey { get; }
    public ButtonControl quoteKey { get; }
    public ButtonControl backquoteKey { get; }
    public ButtonControl commaKey { get; }
    public ButtonControl periodKey { get; }
    public ButtonControl slashKey { get; }
    public ButtonControl f1Key { get; }
    public ButtonControl f2Key { get; }
    public ButtonControl f3Key { get; }
    public ButtonControl f4Key { get; }
    public ButtonControl f5Key { get; }
    public ButtonControl f6Key { get; }
    public ButtonControl f7Key { get; }
    public ButtonControl f8Key { get; }
    public ButtonControl f9Key { get; }
    public ButtonControl f10Key { get; }
    public ButtonControl f11Key { get; }
    public ButtonControl f12Key { get; }
    public ButtonControl WKey { get; }
    public ButtonControl AKey { get; }
    public ButtonControl SKey { get; }
    public ButtonControl DKey { get; }
    public ButtonControl SpaceKey { get; }
    public ButtonControl EnterKey { get; }
    public ButtonControl EscapeKey { get; }
    public ButtonControl LeftShiftKey { get; }
    public ButtonControl LeftCtrlKey { get; }
    public ButtonControl UpArrowKey { get; }
    public ButtonControl DownArrowKey { get; }
    public ButtonControl LeftArrowKey { get; }
    public ButtonControl RightArrowKey { get; }
}
```

## Keire.LightBakeMode

Sources: [Rendering.cs](../../KeireManaged/Rendering.cs)

```csharp
public enum LightBakeMode
{
    Realtime,
    Mixed,
    Baked
}
```

## Keire.LightProbeData

Sources: [NativeAssets.cs](../../KeireManaged/NativeAssets.cs)

```csharp
[StableAssetTypeId("4b454952-454c-5056-4153-534554000001")]
public sealed class LightProbeData : Asset
{
}
```

## Keire.LightProbeVolume

Sources: [BuiltInComponents.cs](../../KeireManaged/BuiltInComponents.cs)

```csharp
[StableComponentId("4b454952-454c-5056-4f4c-554d45000001")]
public sealed class LightProbeVolume : Component
{
    public Vector3 BoxExtents { get; set; }
    public Vector3 Spacing { get; set; }
    public int Priority { get; set; }
    public float NormalBias { get; set; }
    public float ViewBias { get; set; }
}
```

## Keire.LightingQuality

Sources: [RuntimeWorld.cs](../../KeireManaged/RuntimeWorld.cs)

```csharp
public enum LightingQuality : byte
{
    Quality,
    Balanced,
    Performance,
    DirectEnvironment
}
```

## Keire.LightingSet

Sources: [NativeAssets.cs](../../KeireManaged/NativeAssets.cs)

```csharp
[StableAssetTypeId("4b454952-454c-5345-5441-535345540001")]
public sealed class LightingSet : Asset
{
}
```

## Keire.LightingTextureArray

Sources: [NativeAssets.cs](../../KeireManaged/NativeAssets.cs)

```csharp
[StableAssetTypeId("4b454952-454c-5441-5252-415900000001")]
public sealed class LightingTextureArray : Asset
{
}
```

## Keire.LimbContactEvent

Sources: [LimbContacts.cs](../../KeireManaged/LimbContacts.cs)

```csharp
public readonly record struct LimbContactEvent(LimbId Limb, LimbContactEventKind Kind, EntityId Support, Vector3 Position, Vector3 Normal, LimbSupportLoss Reason)
{
}
```

## Keire.LimbContactEventKind

Sources: [LimbContacts.cs](../../KeireManaged/LimbContacts.cs)

```csharp
public enum LimbContactEventKind : byte
{
    Planted,
    Lifted,
    SupportLost,
    Recovered
}
```

## Keire.LimbContactTracker

Sources: [LimbContacts.cs](../../KeireManaged/LimbContacts.cs)

```csharp
public sealed class LimbContactTracker
{
    public LimbId Limb { get; }
    public bool IsPlanted { get; private set; }
    public EntityId Support { get; private set; }
    public Vector3 Position { get; private set; }
    public Vector3 Normal { get; private set; }
    public event Action<LimbContactEvent>? Changed;
    public LimbContactTracker(LimbId limb);
    public void Plant(EntityId support, LimbSupportPose pose, Vector3 worldPosition, Vector3 worldNormal);
    public bool UpdateSupport(EntityId support, LimbSupportPose pose, LimbSupportContinuity limits);
    public bool LoseSupport(LimbSupportLoss reason);
    public void Lift();
}
```

## Keire.LimbGaitGroup

Sources: [LimbGaitScheduler.cs](../../KeireManaged/LimbGaitScheduler.cs)

```csharp
public sealed class LimbGaitGroup
{
    public IReadOnlyList<LimbId> Limbs { get; }
    public LimbGaitGroup(params LimbId[] limbs);
}
```

## Keire.LimbGaitScheduler

Sources: [LimbGaitScheduler.cs](../../KeireManaged/LimbGaitScheduler.cs)

```csharp
public sealed class LimbGaitScheduler
{
    public const int MaximumLimbs;
    public const int MaximumGroups;
    public IReadOnlyList<LimbId> Limbs { get; }
    public int SupportingCount { get; private set; }
    public int SwingingCount { get; private set; }
    public int GroupCount { get; }
    public LimbGaitScheduler(ReadOnlySpan<LimbId> limbs, IReadOnlyList<LimbGaitGroup> preferredGroups, IReadOnlyList<LimbGaitGroup> fallbackGroups, int minimumPlanted, IReadOnlyList<LimbSupportRequirement>? supportRequirements = null, bool preferredRequiresAllPlanted = true);
    public void Initialize(ReadOnlySpan<bool> planted);
    public bool IsPlanted(LimbId limb);
    public bool IsSwinging(LimbId limb);
    public void CompletePlant(LimbId limb);
    public void LoseContact(LimbId limb);
    public int CopyRecoveryCandidates(Span<LimbId> destination);
    public int TryBeginStep(ReadOnlySpan<bool> needsStep, ReadOnlySpan<bool> safeLanding, Span<LimbId> selected, ReadOnlySpan<bool> groupAdmission = default);
}
```

## Keire.LimbId

Sources: [LimbIkRig.cs](../../KeireManaged/LimbIkRig.cs)

```csharp
public readonly record struct LimbId(uint Value)
{
    public bool IsValid { get; }
}
```

## Keire.LimbIkDefinition

Sources: [LimbIkRig.cs](../../KeireManaged/LimbIkRig.cs)

```csharp
public sealed class LimbIkDefinition
{
    public LimbId Id { get; }
    public string Name { get; }
    public IReadOnlyList<string> Bones { get; }
    public LimbIkSolver Solver { get; }
    public uint MaximumIterations { get; }
    public float Tolerance { get; }
    public LimbIkDefinition(LimbId id, string name, IEnumerable<string> bones, LimbIkSolver solver = LimbIkSolver.Fabrik, uint maximumIterations = 32, float tolerance = 0.001f);
}
```

## Keire.LimbIkResult

Sources: [LimbIkResult.cs](../../KeireManaged/LimbIkResult.cs)

```csharp
public readonly record struct LimbIkResult(LimbId Id, LimbIkSolveStatus Status, Vector3 EndPosition, float PositionError, float ReachError, bool JointLimited)
{
}
```

## Keire.LimbIkRig

Sources: [LimbIkRig.cs](../../KeireManaged/LimbIkRig.cs)

```csharp
public sealed class LimbIkRig
{
    public IReadOnlyList<LimbIkDefinition> Limbs { get; }
    public LimbIkRig(Animator animator, string goalNamespace, IEnumerable<LimbIkDefinition> limbs);
    public void SetTarget(LimbId limb, LimbIkTarget target);
    public bool Clear(LimbId limb);
    public void ClearAll();
}
```

## Keire.LimbIkSolveStatus

Sources: [LimbIkResult.cs](../../KeireManaged/LimbIkResult.cs)

```csharp
public enum LimbIkSolveStatus : byte
{
    Disabled,
    Solved,
    Blended,
    Unreachable,
    NotConverged,
    InvalidInput,
    JointLimited
}
```

## Keire.LimbIkSolver

Sources: [LimbIkRig.cs](../../KeireManaged/LimbIkRig.cs)

```csharp
public enum LimbIkSolver : byte
{
    TwoBone,
    Fabrik
}
```

## Keire.LimbIkTarget

Sources: [LimbIkRig.cs](../../KeireManaged/LimbIkRig.cs)

```csharp
public readonly record struct LimbIkTarget(Vector3 Position, Vector3 Pole, float Weight = 1, AnimatorIkSpace Space = AnimatorIkSpace.World)
{
}
```

## Keire.LimbSupportBalance

Sources: [LimbSupportBalance.cs](../../KeireManaged/LimbSupportBalance.cs)

```csharp
public static class LimbSupportBalance
{
    public const int MaximumContacts;
    public static LimbSupportBalanceResult Evaluate(ReadOnlySpan<LimbSupportPoint> contacts, Vector3 bodyPosition, Vector3 planeOrigin, Vector3 planeNormal, Vector3 upDirection, float safetyMargin = 0);
}
```

## Keire.LimbSupportBalanceResult

Sources: [LimbSupportBalance.cs](../../KeireManaged/LimbSupportBalance.cs)

```csharp
public readonly record struct LimbSupportBalanceResult(LimbSupportBalanceStatus Status, int HullVertexCount, double MinimumEdgeClearance, float SafetyMargin)
{
    public bool IsSupported { get; }
}
```

## Keire.LimbSupportBalanceStatus

Sources: [LimbSupportBalance.cs](../../KeireManaged/LimbSupportBalance.cs)

```csharp
public enum LimbSupportBalanceStatus
{
    InsufficientSupportArea,
    OutsideSafetyMargin,
    Supported
}
```

## Keire.LimbSupportContinuity

Sources: [LimbContacts.cs](../../KeireManaged/LimbContacts.cs)

```csharp
public readonly record struct LimbSupportContinuity(float MaximumTranslation, float MaximumRotationDegrees)
{
    public static LimbSupportContinuity Default { get; }
}
```

## Keire.LimbSupportLoss

Sources: [LimbContacts.cs](../../KeireManaged/LimbContacts.cs)

```csharp
public enum LimbSupportLoss : byte
{
    None,
    MissingOrChangedSupport,
    DiscontinuousMotion,
    Unreachable
}
```

## Keire.LimbSupportMotion

Sources: [LimbContacts.cs](../../KeireManaged/LimbContacts.cs)

```csharp
public static class LimbSupportMotion
{
    public static bool TryTransportAnchor(LimbSupportPose previous, LimbSupportPose current, Vector3 localAnchor, LimbSupportContinuity limits, out Vector3 worldAnchor);
}
```

## Keire.LimbSupportPoint

Sources: [LimbSupportBalance.cs](../../KeireManaged/LimbSupportBalance.cs)

```csharp
public readonly record struct LimbSupportPoint(LimbId Limb, Vector3 Position)
{
}
```

## Keire.LimbSupportPose

Sources: [LimbContacts.cs](../../KeireManaged/LimbContacts.cs)

```csharp
public readonly record struct LimbSupportPose(Vector3 Position, Quaternion Rotation)
{
}
```

## Keire.LimbSupportRequirement

Sources: [LimbGaitScheduler.cs](../../KeireManaged/LimbGaitScheduler.cs)

```csharp
public sealed class LimbSupportRequirement
{
    public IReadOnlyList<LimbId> Limbs { get; }
    public int MinimumPlanted { get; }
    public LimbSupportRequirement(int minimumPlanted, params LimbId[] limbs);
}
```

## Keire.Log

Sources: [Debug.cs](../../KeireManaged/Debug.cs)

```csharp
public static class Log
{
    public static void Trace(string message);
    public static void Debug(string message);
    public static void Info(string message);
    public static void Warning(string message);
    public static void Error(string message);
    public static void Critical(string message);
    public static void Trace(string format, params object? [] args);
    public static void Debug(string format, params object? [] args);
    public static void Info(string format, params object? [] args);
    public static void Warning(string format, params object? [] args);
    public static void Error(string format, params object? [] args);
    public static void Critical(string format, params object? [] args);
}
```

## Keire.LogLevel

Sources: [RuntimeApi.cs](../../KeireManaged/RuntimeApi.cs)

```csharp
public enum LogLevel : byte
{
    Trace,
    Debug,
    Information,
    Warning,
    Error,
    Critical
}
```

## Keire.ManagedMigrationContext

Sources: [ManagedCustomSerialization.cs](../../KeireManaged/ManagedCustomSerialization.cs)

```csharp
public sealed record ManagedMigrationContext(Guid StableId, uint FromVersion, uint ToVersion, string Path)
{
}
```

## Keire.ManagedSerialization

Sources: [ManagedSerialization.cs](../../KeireManaged/ManagedSerialization.cs)

```csharp
public static class ManagedSerialization
{
    public static void ValidateValue(object? value, Type declaredType, string path, bool preserveReferences = false);
}
```

## Keire.ManagedSerializationContext

Sources: [ManagedCustomSerialization.cs](../../KeireManaged/ManagedCustomSerialization.cs)

```csharp
public sealed record ManagedSerializationContext(string Path, string Phase)
{
}
```

## Keire.ManagedSerializationException

Sources: [ManagedSerializationException.cs](../../KeireManaged/ManagedSerializationException.cs)

```csharp
public sealed class ManagedSerializationException : InvalidOperationException
{
    public string Code { get; }
    public string Phase { get; private set; }
    public string Owner { get; private set; }
    public string RootField { get; private set; }
    public string FieldPath { get; }
    public Type DeclaredType { get; }
    public Type? RuntimeType { get; }
    public string? SerializedTypeId { get; private set; }
    public int? ObjectId { get; private set; }
    public string Reason { get; }
}
```

## Keire.ManagedSerializedValue

Sources: [ManagedCustomSerialization.cs](../../KeireManaged/ManagedCustomSerialization.cs)

```csharp
public sealed class ManagedSerializedValue
{
    public ManagedSerializedValueKind Kind { get; }
    public static ManagedSerializedValue Null { get; }
    public static ManagedSerializedValue From(bool value);
    public static ManagedSerializedValue From(long value);
    public static ManagedSerializedValue From(ulong value);
    public static ManagedSerializedValue From(double value);
    public static ManagedSerializedValue From(string value);
    public static ManagedSerializedValue FromList(IEnumerable<ManagedSerializedValue> values);
    public static ManagedSerializedValue FromMap(IEnumerable<KeyValuePair<string, ManagedSerializedValue>> values);
    public bool AsBoolean();
    public long AsInt64();
    public ulong AsUInt64();
    public double AsNumber();
    public string AsString();
    public IReadOnlyList<ManagedSerializedValue> AsList();
    public IReadOnlyDictionary<string, ManagedSerializedValue> AsMap();
}
```

## Keire.ManagedSerializedValueKind

Sources: [ManagedCustomSerialization.cs](../../KeireManaged/ManagedCustomSerialization.cs)

```csharp
public enum ManagedSerializedValueKind
{
    Null,
    Boolean,
    SignedInteger,
    UnsignedInteger,
    Number,
    String,
    List,
    Map
}
```

## Keire.ManagedValueConverter

Sources: [ManagedCustomSerialization.cs](../../KeireManaged/ManagedCustomSerialization.cs)

```csharp
public abstract class ManagedValueConverter
{
}
```

## Keire.ManagedValueConverter of T

Sources: [ManagedCustomSerialization.cs](../../KeireManaged/ManagedCustomSerialization.cs)

```csharp
public abstract class ManagedValueConverter<T> : ManagedValueConverter
{
    public abstract ManagedSerializedValue Write(T value, ManagedSerializationContext context);
    public abstract T Read(ManagedSerializedValue value, ManagedSerializationContext context);
}
```

## Keire.ManagedValueMigrationAttribute

Sources: [ManagedCustomSerialization.cs](../../KeireManaged/ManagedCustomSerialization.cs)

```csharp
[AttributeUsage(AttributeTargets.Class, Inherited = false)]
public sealed class ManagedValueMigrationAttribute : Attribute
{
    public ManagedValueMigrationAttribute(string stableId, uint fromVersion, uint toVersion);
    public Guid StableId { get; }
    public uint FromVersion { get; }
    public uint ToVersion { get; }
}
```

## Keire.Material

Sources: [Rendering.cs](../../KeireManaged/Rendering.cs)

```csharp
[StableAssetTypeId("4b454952-454d-4154-4552-49414c000001")]
public sealed class Material : Asset
{
}
```

## Keire.MaterialFunction

Sources: [NativeAssets.cs](../../KeireManaged/NativeAssets.cs)

```csharp
[StableAssetTypeId("4b454952-454d-4655-4e43-54494f4e0001")]
public sealed class MaterialFunction : Asset
{
}
```

## Keire.MaterialGraph

Sources: [Rendering.cs](../../KeireManaged/Rendering.cs)

```csharp
[StableAssetTypeId("4b454952-454d-4752-4150-480000000001")]
public sealed class MaterialGraph : Asset
{
}
```

## Keire.MaterialInstance

Sources: [Rendering.cs](../../KeireManaged/Rendering.cs)

```csharp
[StableAssetTypeId("4b454952-454d-494e-5354-414e43450001")]
public sealed class MaterialInstance : Asset
{
}
```

## Keire.MaterialLayer

Sources: [NativeAssets.cs](../../KeireManaged/NativeAssets.cs)

```csharp
[StableAssetTypeId("4b454952-454d-4c41-5945-520000000001")]
public sealed class MaterialLayer : Asset
{
}
```

## Keire.MaterialLayerBlend

Sources: [NativeAssets.cs](../../KeireManaged/NativeAssets.cs)

```csharp
[StableAssetTypeId("4b454952-454d-4c42-4c45-4e4400000001")]
public sealed class MaterialLayerBlend : Asset
{
}
```

## Keire.MaterialParameterCollection

Sources: [Rendering.cs](../../KeireManaged/Rendering.cs)

```csharp
[StableAssetTypeId("4b454952-454d-5043-4f4c-4c4543540001")]
public sealed class MaterialParameterCollection : Asset
{
}
```

## Keire.MaterialParameterCollectionInstance

Sources: [Rendering.cs](../../KeireManaged/Rendering.cs)

```csharp
public sealed class MaterialParameterCollectionInstance
{
    public MaterialParameterCollection Asset { get; }
    public bool IsReady { get; }
    public void SetFloat(string name, float value);
    public void SetVector(string name, Vector2 value);
    public void SetVector(string name, Vector3 value);
    public void SetVector(string name, Vector4 value);
    public void SetColor(string name, Color value);
    public void SetTexture(string name, Texture? value);
    public bool Reset(string name);
    public void Clear();
}
```

## Keire.MaterialPropertyBlock

Sources: [Rendering.cs](../../KeireManaged/Rendering.cs)

```csharp
public readonly record struct MaterialPropertyBlock(Entity Entity)
{
    public void SetFloat(string name, float value);
    public void SetVector(string name, Vector2 value);
    public void SetVector(string name, Vector3 value);
    public void SetVector(string name, Vector4 value);
    public void SetColor(string name, Color value);
    public void SetTexture(string name, Texture? value);
    public bool Reset(string name);
    public void Clear();
}
```

## Keire.MaxAttribute

Sources: [SerializationAttributes.cs](../../KeireManaged/SerializationAttributes.cs)

```csharp
[AttributeUsage(AttributeTargets.Field | AttributeTargets.Property)]
public sealed class MaxAttribute : Attribute
{
    public MaxAttribute(double maximum);
    public readonly double Maximum;
}
```

## Keire.Mesh

Sources: [Rendering.cs](../../KeireManaged/Rendering.cs)

```csharp
[StableAssetTypeId("4b454952-454d-4553-4841-535345540001")]
public sealed class Mesh : Asset
{
}
```

## Keire.MeshRenderer

Sources: [Rendering.cs](../../KeireManaged/Rendering.cs)

```csharp
[StableComponentId("4b454952-454d-4553-4852-454e44455201")]
public sealed class MeshRenderer : Component
{
    public Mesh? Mesh { get; set; }
    public IReadOnlyList<Material> Materials { get; set; }
    public Material? Material { get; set; }
    public Color Tint { get; set; }
    public bool Visible { get; set; }
    public bool AlwaysVisible { get; set; }
    public bool CastShadows { get; set; }
    public bool ReceiveShadows { get; set; }
    public bool StaticLighting { get; set; }
    public bool PreserveLightmapUVs { get; set; }
    public GIReceiveMode GIReceive { get; set; }
    public float LightmapScale { get; set; }
    public MaterialPropertyBlock PropertyBlock { get; }
    public DynamicMaterial GetMaterialInstance(int materialSlot = 0);
}
```

## Keire.MinAttribute

Sources: [SerializationAttributes.cs](../../KeireManaged/SerializationAttributes.cs)

```csharp
[AttributeUsage(AttributeTargets.Field | AttributeTargets.Property)]
public sealed class MinAttribute : Attribute
{
    public MinAttribute(double minimum);
    public readonly double Minimum;
}
```

## Keire.Mouse

Sources: [DirectInput.cs](../../KeireManaged/DirectInput.cs)

```csharp
public sealed class Mouse
{
    public uint DeviceId { get; }
    public static Mouse? Current { get; }
    public Vector2Control Position { get; }
    public Vector2Control Delta { get; }
    public Vector2Control Scroll { get; }
    public ButtonControl LeftButton { get; }
    public ButtonControl RightButton { get; }
    public ButtonControl MiddleButton { get; }
    public ButtonControl BackButton { get; }
    public ButtonControl ForwardButton { get; }
    public ButtonControl WheelUp { get; }
    public ButtonControl WheelDown { get; }
    public Vector2Control position { get; }
    public Vector2Control delta { get; }
    public Vector2Control scroll { get; }
    public ButtonControl leftButton { get; }
    public ButtonControl rightButton { get; }
}
```

## Keire.MultilineAttribute

Sources: [SerializationAttributes.cs](../../KeireManaged/SerializationAttributes.cs)

```csharp
[AttributeUsage(AttributeTargets.Field | AttributeTargets.Property)]
public sealed class MultilineAttribute : Attribute
{
    public MultilineAttribute(int lines = 4);
    public readonly int Lines;
}
```

## Keire.NativeAbiValue

Sources: [NativeServiceRuntime.cs](../../KeireManaged/NativeServiceRuntime.cs)

```csharp
public readonly struct NativeAbiValue
{
    public NativeValueKind Kind { get; }
    public static NativeAbiValue From(bool value);
    public static NativeAbiValue From(sbyte value);
    public static NativeAbiValue From(short value);
    public static NativeAbiValue From(int value);
    public static NativeAbiValue From(long value);
    public static NativeAbiValue From(byte value);
    public static NativeAbiValue From(ushort value);
    public static NativeAbiValue From(uint value);
    public static NativeAbiValue From(ulong value);
    public static NativeAbiValue From(float value);
    public static NativeAbiValue From(double value);
    public static NativeAbiValue From(string value);
    public static NativeAbiValue From(Vector2 value);
    public static NativeAbiValue From(Vector3 value);
    public static NativeAbiValue From(Vector4 value);
    public static NativeAbiValue From(Quaternion value);
    public static NativeAbiValue From(Color value);
    public static NativeAbiValue From(Guid value);
    public static NativeAbiValue From(EntityId value);
    public static NativeAbiValue From(AssetId value);
    public static NativeAbiValue From(ComponentTypeId value);
    public static NativeAbiValue FromBuffer<T>(ReadOnlySpan<T> values, int maximumElements);
}
```

## Keire.NativeBufferAttribute

Sources: [NativeServiceContracts.cs](../../KeireManaged/NativeServiceContracts.cs)

```csharp
[AttributeUsage(AttributeTargets.Parameter, Inherited = false)]
public sealed class NativeBufferAttribute : Attribute
{
    public NativeBufferAttribute(int maximumElements);
    public int MaximumElements { get; }
}
```

## Keire.NativeCallError

Sources: [NativeServiceContracts.cs](../../KeireManaged/NativeServiceContracts.cs)

```csharp
public readonly record struct NativeCallError
{
    public NativeCallError(string code, string message);
    public string Code { get; }
    public string Message { get; }
}
```

## Keire.NativeCallResult of T

Sources: [NativeServiceContracts.cs](../../KeireManaged/NativeServiceContracts.cs)

```csharp
public readonly struct NativeCallResult<T>
{
    public bool IsSuccess { get; }
    public T? Value { get; }
    public NativeCallError? Error { get; }
    public static NativeCallResult<T> Success(T value);
    public static NativeCallResult<T> Failure(string code, string message);
}
```

## Keire.NativeMethodAttribute

Sources: [NativeServiceContracts.cs](../../KeireManaged/NativeServiceContracts.cs)

```csharp
[AttributeUsage(AttributeTargets.Method, Inherited = false)]
public sealed class NativeMethodAttribute : Attribute
{
    public NativeMethodAttribute(string stableId, uint abiVersion = 1, NativeThreadAffinity threadAffinity = NativeThreadAffinity.ManagedOwnerThread);
    public Guid StableId { get; }
    public uint AbiVersion { get; }
    public NativeThreadAffinity ThreadAffinity { get; }
}
```

## Keire.NativeMethodDescriptor

Sources: [NativeServiceContracts.cs](../../KeireManaged/NativeServiceContracts.cs)

```csharp
public sealed record NativeMethodDescriptor(Guid StableId, uint AbiVersion, string Name, NativeThreadAffinity ThreadAffinity, NativeValueKind ReturnKind, NativeValueKind StructuredResultKind, IReadOnlyList<NativeParameterDescriptor> Parameters)
{
}
```

## Keire.NativeParameterDescriptor

Sources: [NativeServiceContracts.cs](../../KeireManaged/NativeServiceContracts.cs)

```csharp
public sealed record NativeParameterDescriptor(string Name, string ManagedType, NativeValueKind Kind, NativeValueKind ElementKind, int MaximumElements)
{
}
```

## Keire.NativeServiceContractAttribute

Sources: [NativeServiceContracts.cs](../../KeireManaged/NativeServiceContracts.cs)

```csharp
[AttributeUsage(AttributeTargets.Interface, Inherited = false)]
public sealed class NativeServiceContractAttribute : Attribute
{
    public NativeServiceContractAttribute(string stableId, uint abiVersion = 1);
    public Guid StableId { get; }
    public uint AbiVersion { get; }
}
```

## Keire.NativeServiceDescriptor

Sources: [NativeServiceContracts.cs](../../KeireManaged/NativeServiceContracts.cs)

```csharp
public sealed record NativeServiceDescriptor(Guid StableId, uint AbiVersion, string ManagedType, IReadOnlyList<NativeMethodDescriptor> Methods)
{
}
```

## Keire.NativeServiceRuntime

Sources: [NativeServiceRuntime.cs](../../KeireManaged/NativeServiceRuntime.cs)

```csharp
[EditorBrowsable(EditorBrowsableState.Never)]
public static class NativeServiceRuntime
{
    public static T Invoke<T>(Guid serviceId, Guid methodId, ReadOnlySpan<NativeAbiValue> arguments);
    public static void Invoke(Guid serviceId, Guid methodId, ReadOnlySpan<NativeAbiValue> arguments);
    public static NativeCallResult<T> InvokeResult<T>(Guid serviceId, Guid methodId, ReadOnlySpan<NativeAbiValue> arguments);
}
```

## Keire.NativeThreadAffinity

Sources: [NativeServiceContracts.cs](../../KeireManaged/NativeServiceContracts.cs)

```csharp
public enum NativeThreadAffinity : byte
{
    AnyThread,
    ManagedOwnerThread,
    MainThread
}
```

## Keire.NativeValueKind

Sources: [NativeServiceContracts.cs](../../KeireManaged/NativeServiceContracts.cs)

```csharp
public enum NativeValueKind : byte
{
    Void,
    Boolean,
    SignedInteger,
    UnsignedInteger,
    FloatingPoint,
    Utf8String,
    Vector2,
    Vector3,
    Vector4,
    Quaternion,
    Color,
    StableId,
    EntityId,
    AssetId,
    ComponentTypeId,
    BoundedBuffer,
    StructuredResult
}
```

## Keire.Navigation

Sources: [RuntimeApi.cs](../../KeireManaged/RuntimeApi.cs)

```csharp
public static class Navigation
{
    public static ValueTask<NavigationPath> FindPathAsync(Vector3 start, Vector3 end, uint areaMask = uint.MaxValue, CancellationToken cancellation = default);
}
```

## Keire.NavigationPath

Sources: [RuntimeApi.cs](../../KeireManaged/RuntimeApi.cs)

```csharp
public readonly record struct NavigationPath(IReadOnlyList<Vector3> Points, ulong MeshRevision)
{
}
```

## Keire.PersistentEventCall

Sources: [Events.cs](../../KeireManaged/Events.cs)

```csharp
[SerializableType]
public sealed class PersistentEventCall
{
    public bool Enabled;
    public Entity? Target;
    public ComponentTypeId Component;
    public string Method;
}
```

## Keire.Physics

Sources: [RuntimeApi.cs](../../KeireManaged/RuntimeApi.cs)

```csharp
public static class Physics
{
    public static bool TryRaycast(Entity context, Vector3 origin, Vector3 direction, out RaycastHit hit, float maximumDistance = 1000.0f, uint mask = uint.MaxValue, Entity? ignoredEntity = null);
    public static IReadOnlyList<RaycastHit> Raycast(Entity context, Vector3 origin, Vector3 direction, float maximumDistance = 1000.0f, uint mask = uint.MaxValue);
    public static bool TryCapsuleCast(Entity context, Vector3 origin, Quaternion rotation, float radius, float height, Vector3 displacement, out RaycastHit hit, uint mask = uint.MaxValue, bool includeTriggers = false, Entity? ignoredEntity = null);
    public static IReadOnlyList<Entity> OverlapSphere(Entity context, Vector3 center, float radius, uint mask = uint.MaxValue, bool includeTriggers = true, Entity? ignoredEntity = null);
}
```

## Keire.PhysicsMaterial

Sources: [BuiltInComponents.cs](../../KeireManaged/BuiltInComponents.cs)

```csharp
[StableAssetTypeId("4b454952-4550-4859-534d-415445520001")]
public sealed class PhysicsMaterial : Asset
{
}
```

## Keire.PlayerPreferences

Sources: [PlayerPreferences.cs](../../KeireManaged/PlayerPreferences.cs)

```csharp
public static class PlayerPreferences
{
    public static bool HasKey(string key);
    public static string GetString(string key, string defaultValue = "");
    public static int GetInt(string key, int defaultValue = 0);
    public static float GetFloat(string key, float defaultValue = 0.0f);
    public static bool GetBool(string key, bool defaultValue = false);
    public static void SetString(string key, string value);
    public static void SetInt(string key, int value);
    public static void SetFloat(string key, float value);
    public static void SetBool(string key, bool value);
    public static bool DeleteKey(string key);
    public static void DeleteAll();
    public static void Save();
}
```

## Keire.PointLight

Sources: [Rendering.cs](../../KeireManaged/Rendering.cs)

```csharp
[StableComponentId("4b454952-4550-4f49-4e54-4c4947485401")]
public sealed class PointLight : Component
{
    public Color Color { get; set; }
    public float Intensity { get; set; }
    public float Range { get; set; }
    public ShadowQuality Shadows { get; set; }
    public float ShadowStrength { get; set; }
    public float ShadowBias { get; set; }
    public LightBakeMode BakeMode { get; set; }
    public ShadowResolution ShadowResolution { get; set; }
    public Texture? Cookie { get; set; }
    public bool ContactShadows { get; set; }
    public float IndirectMultiplier { get; set; }
}
```

## Keire.Prefab

Sources: [RuntimeApi.cs](../../KeireManaged/RuntimeApi.cs)

```csharp
[StableAssetTypeId("4b454952-4550-5245-4641-424153535401")]
public sealed class Prefab : Asset
{
    public Entity Instantiate(Vector3 position = default, Quaternion rotation = default, Entity? parent = null, bool active = true);
    public Entity Instantiate(Entity parent, bool active = true);
}
```

## Keire.PresentMode

Sources: [RuntimeFoundation.cs](../../KeireManaged/RuntimeFoundation.cs)

```csharp
public enum PresentMode : byte
{
    VSync,
    Mailbox,
    Immediate
}
```

## Keire.ProceduralFootSide

Sources: [Behaviour.cs](../../KeireManaged/Behaviour.cs)

```csharp
public enum ProceduralFootSide : byte
{
    None,
    Left,
    Right
}
```

## Keire.ProceduralLocomotionIntent

Sources: [RuntimeApi.cs](../../KeireManaged/RuntimeApi.cs)

```csharp
public readonly record struct ProceduralLocomotionIntent(Vector3 DesiredWorldVelocity, Vector3 FacingWorldDirection, Vector3 LookWorldDirection, float CrouchAmount, float RunBlend, bool JumpRequested)
{
}
```

## Keire.ProceduralLocomotionState

Sources: [RuntimeApi.cs](../../KeireManaged/RuntimeApi.cs)

```csharp
public readonly record struct ProceduralLocomotionState(ProceduralMotionState State, ProceduralMotionQuality Quality, Vector3 ActualWorldVelocity, Vector3 GroundNormal, float GaitPhase, float Speed, float VerticalSpeed, float LandingIntensity, bool Grounded, bool LeftFootPlanted, bool RightFootPlanted)
{
}
```

## Keire.ProceduralMotionEvent

Sources: [Behaviour.cs](../../KeireManaged/Behaviour.cs)

```csharp
public readonly record struct ProceduralMotionEvent(ProceduralMotionEventType Type, ProceduralFootSide Foot, ProceduralMotionState State, float Phase, float Intensity, Vector3 ContactPosition, Vector3 ContactNormal, Entity Support, AssetId PhysicsMaterial)
{
}
```

## Keire.ProceduralMotionEventType

Sources: [Behaviour.cs](../../KeireManaged/Behaviour.cs)

```csharp
public enum ProceduralMotionEventType : byte
{
    FootLift,
    FootPlant,
    Takeoff,
    Apex,
    Land,
    StateChanged
}
```

## Keire.ProceduralMotionProfile

Sources: [NativeAssets.cs](../../KeireManaged/NativeAssets.cs)

```csharp
[StableAssetTypeId("4b454952-4550-524f-434d-4f54494f4e01")]
public sealed class ProceduralMotionProfile : Asset
{
}
```

## Keire.ProceduralMotionQuality

Sources: [RuntimeApi.cs](../../KeireManaged/RuntimeApi.cs)

```csharp
public enum ProceduralMotionQuality : byte
{
    Auto,
    High,
    Medium,
    Low
}
```

## Keire.ProceduralMotionState

Sources: [RuntimeApi.cs](../../KeireManaged/RuntimeApi.cs)

```csharp
public enum ProceduralMotionState : byte
{
    Idle,
    Locomotion,
    TurnInPlace,
    Takeoff,
    Rising,
    Falling,
    Landing
}
```

## Keire.ProfileSample

Sources: [Profiler.cs](../../KeireManaged/Profiler.cs)

```csharp
public readonly struct ProfileSample : IDisposable
{
    public void Dispose();
}
```

## Keire.Profiler

Sources: [Profiler.cs](../../KeireManaged/Profiler.cs)

```csharp
public static class Profiler
{
    public static ProfileSample Sample(string name);
    public static void Counter(string name, double value);
}
```

## Keire.Quaternion

Sources: [MathTypes.cs](../../KeireManaged/MathTypes.cs)

```csharp
public readonly record struct Quaternion(float X, float Y, float Z, float W)
{
    public static Quaternion Identity { get; }
    public Quaternion Normalized { get; }
    public static Quaternion Euler(float pitchDegrees, float yawDegrees, float rollDegrees = 0.0f);
    public static Quaternion operator *(Quaternion left, Quaternion right);
    public static Vector3 operator *(Quaternion rotation, Vector3 point);
    public override string ToString();
}
```

## Keire.RangeAttribute

Sources: [SerializationAttributes.cs](../../KeireManaged/SerializationAttributes.cs)

```csharp
[AttributeUsage(AttributeTargets.Field | AttributeTargets.Property)]
public sealed class RangeAttribute : Attribute
{
    public RangeAttribute(double minimum, double maximum);
    public readonly double Minimum;
    public readonly double Maximum;
}
```

## Keire.RaycastHit

Sources: [RuntimeApi.cs](../../KeireManaged/RuntimeApi.cs)

```csharp
public readonly record struct RaycastHit(Entity Entity, Vector3 Point, Vector3 Normal, float Distance)
{
}
```

## Keire.ReadOnlyInInspectorAttribute

Sources: [SerializationAttributes.cs](../../KeireManaged/SerializationAttributes.cs)

```csharp
[AttributeUsage(AttributeTargets.Field | AttributeTargets.Property)]
public sealed class ReadOnlyInInspectorAttribute : Attribute
{
}
```

## Keire.ReflectionProbe

Sources: [BuiltInComponents.cs](../../KeireManaged/BuiltInComponents.cs)

```csharp
[StableComponentId("4b454952-4552-4546-4c50-524f42450001")]
public sealed class ReflectionProbe : Component
{
    public ReflectionProbeCaptureMode CaptureMode { get; set; }
    public ReflectionProbeResolution Resolution { get; set; }
    public Vector3 BoxExtents { get; set; }
    public float BlendDistance { get; set; }
    public int Importance { get; set; }
    public float Intensity { get; set; }
    public bool BoxProjection { get; set; }
    public bool IncludeSky { get; set; }
}
```

## Keire.ReflectionProbeCaptureMode

Sources: [BuiltInComponents.cs](../../KeireManaged/BuiltInComponents.cs)

```csharp
public enum ReflectionProbeCaptureMode : byte
{
    Baked,
    OnDemand
}
```

## Keire.ReflectionProbeResolution

Sources: [BuiltInComponents.cs](../../KeireManaged/BuiltInComponents.cs)

```csharp
public enum ReflectionProbeResolution : ushort
{
    Size64 = 64,
    Size128 = 128,
    Size256 = 256,
    Size512 = 512
}
```

## Keire.RenderEnvironmentSettings

Sources: [RuntimeWorld.cs](../../KeireManaged/RuntimeWorld.cs)

```csharp
public readonly record struct RenderEnvironmentSettings
{
    public static RenderEnvironmentSettings Default { get; }
    public Color AmbientColor { get; init; }
    public float AmbientIntensity { get; init; }
    public float Exposure { get; init; }
    public Texture? Environment { get; init; }
    public float EnvironmentRotationDegrees { get; init; }
    public float EnvironmentDiffuseIntensity { get; init; }
    public float EnvironmentSpecularIntensity { get; init; }
    public bool SkyVisible { get; init; }
    public float DirectionalShadowDistance { get; init; }
    public uint DirectionalShadowCascadeCount { get; init; }
    public uint DirectionalShadowResolution { get; init; }
    public float DirectionalShadowSplitLambda { get; init; }
}
```

## Keire.RenderSettings

Sources: [RuntimeWorld.cs](../../KeireManaged/RuntimeWorld.cs)

```csharp
public static class RenderSettings
{
    public static LightingQuality LightingQuality { get; set; }
    public static RenderEnvironmentSettings Current { get; set; }
    public static Color AmbientColor { get; set; }
    public static float AmbientIntensity { get; set; }
    public static float Exposure { get; set; }
    public static Texture? Environment { get; set; }
    public static bool SkyVisible { get; set; }
}
```

## Keire.RequireComponentAttribute

Sources: [SerializationAttributes.cs](../../KeireManaged/SerializationAttributes.cs)

```csharp
[AttributeUsage(AttributeTargets.Class, AllowMultiple = true, Inherited = true)]
public sealed class RequireComponentAttribute : Attribute
{
    public RequireComponentAttribute(Type componentType);
    public Type ComponentType { get; }
    public readonly ulong High;
    public readonly ulong Low;
}
```

## Keire.Resolution

Sources: [RuntimeFoundation.cs](../../KeireManaged/RuntimeFoundation.cs)

```csharp
public readonly record struct Resolution(uint Width, uint Height, uint PixelWidth, uint PixelHeight, float DisplayScale)
{
}
```

## Keire.RigDefinition

Sources: [NativeAssets.cs](../../KeireManaged/NativeAssets.cs)

```csharp
[StableAssetTypeId("4b454952-4552-4947-4445-460000000001")]
public sealed class RigDefinition : Asset
{
}
```

## Keire.RigidBody

Sources: [RuntimeApi.cs](../../KeireManaged/RuntimeApi.cs)

```csharp
[StableComponentId("4b454952-4552-4947-4944-424f44590001")]
public sealed class RigidBody : Component
{
    public RigidBodyMotion Motion { get; set; }
    public float Mass { get; set; }
    public Vector3 Velocity { get; set; }
    public bool Continuous { get; set; }
    public bool UseGravity { get; set; }
    public void AddForce(Vector3 force, ForceMode mode = ForceMode.Force);
    public void AddImpulse(Vector3 impulse);
}
```

## Keire.RigidBodyMotion

Sources: [RuntimeApi.cs](../../KeireManaged/RuntimeApi.cs)

```csharp
public enum RigidBodyMotion : byte
{
    Static,
    Dynamic,
    Kinematic
}
```

## Keire.RigidBodyProperties

Sources: [RuntimeApi.cs](../../KeireManaged/RuntimeApi.cs)

```csharp
public readonly record struct RigidBodyProperties(RigidBodyMotion Motion, float Mass, Vector3 Velocity, bool Continuous, bool UseGravity)
{
}
```

## Keire.RuntimeBridge

Sources: [RuntimeApi.cs](../../KeireManaged/RuntimeApi.cs)

```csharp
public static class RuntimeBridge
{
    public static IRuntimeBridge Current { get; }
    public static void Install(IRuntimeBridge bridge);
}
```

## Keire.RuntimeServiceAttribute

Sources: [RuntimeServices.cs](../../KeireManaged/RuntimeServices.cs)

```csharp
[AttributeUsage(AttributeTargets.Class, Inherited = false)]
public sealed class RuntimeServiceAttribute : Attribute
{
    public RuntimeServiceAttribute(string stableId, bool optional = false);
    public Guid StableId { get; }
    public bool Optional { get; }
}
```

## Keire.RuntimeServiceContext

Sources: [RuntimeServices.cs](../../KeireManaged/RuntimeServices.cs)

```csharp
public sealed class RuntimeServiceContext
{
    public ulong Generation { get; }
    public CancellationToken LifetimeToken { get; }
    public T GetRequiredService<T>()
        where T : class, IRuntimeService;
    public T? GetService<T>()
        where T : class, IRuntimeService;
}
```

## Keire.RuntimeServiceDependencyAttribute

Sources: [RuntimeServices.cs](../../KeireManaged/RuntimeServices.cs)

```csharp
[AttributeUsage(AttributeTargets.Class, AllowMultiple = true, Inherited = false)]
public sealed class RuntimeServiceDependencyAttribute : Attribute
{
    public RuntimeServiceDependencyAttribute(Type serviceType, bool optional = false);
    public Type ServiceType { get; }
    public bool Optional { get; }
}
```

## Keire.RuntimeServiceDiagnostic

Sources: [RuntimeServices.cs](../../KeireManaged/RuntimeServices.cs)

```csharp
public sealed record RuntimeServiceDiagnostic(Guid StableId, string ServiceType, string Phase, string Message, bool Quarantined)
{
}
```

## Keire.RuntimeServiceHost

Sources: [RuntimeServices.cs](../../KeireManaged/RuntimeServices.cs)

```csharp
public sealed class RuntimeServiceHost : IDisposable
{
    public ulong Generation { get; }
    public CancellationToken LifetimeToken { get; }
    public IReadOnlyList<RuntimeServiceDiagnostic> Diagnostics { get; }
    public IReadOnlyList<Guid> OrderedServiceIds { get; }
    public static RuntimeServiceHost Start(ulong generation, IEnumerable<Type> exactAllowedTypes, IReadOnlyDictionary<Guid, ManagedSerializedValue>? previousState = null);
    public T? GetService<T>()
        where T : class, IRuntimeService;
    public void Update(double deltaSeconds, double unscaledDeltaSeconds, ulong frame);
    public IReadOnlyDictionary<Guid, ManagedSerializedValue> CaptureHotReloadState();
    public void Dispose();
}
```

## Keire.RuntimeServiceUpdateContext

Sources: [RuntimeServices.cs](../../KeireManaged/RuntimeServices.cs)

```csharp
public readonly record struct RuntimeServiceUpdateContext
{
    public RuntimeServiceUpdateContext(double deltaSeconds, double unscaledDeltaSeconds, ulong frame, CancellationToken lifetimeToken);
    public double DeltaSeconds { get; }
    public double UnscaledDeltaSeconds { get; }
    public ulong Frame { get; }
    public CancellationToken LifetimeToken { get; }
}
```

## Keire.Scene

Sources: [RuntimeWorld.cs](../../KeireManaged/RuntimeWorld.cs)

```csharp
public sealed class Scene : EngineObject, IEquatable<Scene>
{
    public SceneAsset Asset { get; }
    public ulong Id { get; }
    public override bool IsValid { get; }
    public bool HasStableIdentity { get; }
    public bool IsLoaded { get; }
    public bool IsActive { get; }
    public bool Equals(Scene? other);
    public override bool Equals(object? value);
    public override int GetHashCode();
}
```

## Keire.SceneAsset

Sources: [RuntimeWorld.cs](../../KeireManaged/RuntimeWorld.cs)

```csharp
public sealed class SceneAsset : Asset
{
}
```

## Keire.SceneLoadMode

Sources: [RuntimeWorld.cs](../../KeireManaged/RuntimeWorld.cs)

```csharp
public enum SceneLoadMode : byte
{
    Single,
    Additive
}
```

## Keire.SceneLoadOperation

Sources: [RuntimeWorld.cs](../../KeireManaged/RuntimeWorld.cs)

```csharp
public sealed class SceneLoadOperation : CustomYieldInstruction
{
    public Scene? Scene { get; }
    public SceneLoadMode Mode { get; }
    public SceneLoadState State { get; }
    public float Progress { get; }
    public bool IsDone { get; }
    public bool Succeeded { get; }
    public string Error { get; }
    public override bool KeepWaiting { get; }
    public bool Cancel();
}
```

## Keire.SceneLoadState

Sources: [RuntimeWorld.cs](../../KeireManaged/RuntimeWorld.cs)

```csharp
public enum SceneLoadState : byte
{
    Queued,
    Loading,
    Ready,
    Failed,
    Cancelled
}
```

## Keire.SceneManager

Sources: [RuntimeWorld.cs](../../KeireManaged/RuntimeWorld.cs)

```csharp
public static class SceneManager
{
    public static Scene? ActiveScene { get; }
    public static IReadOnlyList<Scene> LoadedScenes { get; }
    public static Entity? FindByName(string name);
    public static IReadOnlyList<Entity> FindAllByName(string name, int maximumResults = 256);
    public static IReadOnlyList<Entity> FindAllByName(string name, SceneQuery query, int maximumResults = 256);
    public static Entity? FindWithTag(string tag);
    public static IReadOnlyList<Entity> FindAllWithTag(string tag, int maximumResults = 256);
    public static IReadOnlyList<Entity> FindAllWithTag(string tag, SceneQuery query, int maximumResults = 256);
    public static IReadOnlyList<Entity> FindAllWithComponent<T>(int maximumResults = 256);
    public static IReadOnlyList<Entity> FindAllWithComponent<T>(SceneQuery query, int maximumResults = 256);
    public static SceneLoadOperation LoadSceneAsync(SceneAsset scene, SceneLoadMode mode = SceneLoadMode.Single);
    public static SceneLoadOperation LoadSceneAsync(AssetId scene, SceneLoadMode mode = SceneLoadMode.Single);
    public static bool UnloadScene(Scene scene);
    public static bool SetActiveScene(Scene scene);
    public static bool Preserve(Entity entity);
}
```

## Keire.SceneQuery

Sources: [RuntimeWorld.cs](../../KeireManaged/RuntimeWorld.cs)

```csharp
public readonly record struct SceneQuery
{
    public SceneQueryScope Scope { get; }
    public Scene? Scene { get; }
    public static SceneQuery Active { get; }
    public static SceneQuery Loaded { get; }
    public static SceneQuery Persistent { get; }
    public static SceneQuery In(Scene scene);
}
```

## Keire.SceneQueryScope

Sources: [RuntimeWorld.cs](../../KeireManaged/RuntimeWorld.cs)

```csharp
public enum SceneQueryScope : byte
{
    Active,
    Loaded,
    Persistent,
    Specific
}
```

## Keire.Screen

Sources: [RuntimeFoundation.cs](../../KeireManaged/RuntimeFoundation.cs)

```csharp
public static class Screen
{
    public static Resolution CurrentResolution { get; }
    public static uint Width { get; }
    public static uint Height { get; }
    public static float DisplayScale { get; }
    public static FullscreenMode Mode { get; }
    public static bool Fullscreen { get; }
    public static bool Focused { get; }
    public static bool Visible { get; }
    public static bool Minimized { get; }
    public static bool PresentationAvailable { get; }
    public static PresentMode PresentMode { get; set; }
    public static bool VSyncEnabled { get; set; }
    public static bool IsPresentModeSupported(PresentMode mode);
    public static bool TrySetPresentMode(PresentMode mode);
    public static void SetPresentMode(PresentMode mode);
    public static ScreenRect SafeArea { get; }
    public static bool TrySetResolution(uint width, uint height, FullscreenMode mode = FullscreenMode.Windowed);
    public static void SetResolution(uint width, uint height, FullscreenMode mode = FullscreenMode.Windowed);
}
```

## Keire.ScreenRect

Sources: [RuntimeFoundation.cs](../../KeireManaged/RuntimeFoundation.cs)

```csharp
public readonly record struct ScreenRect(float X, float Y, float Width, float Height)
{
}
```

## Keire.ScriptableObject

Sources: [ScriptableObject.cs](../../KeireManaged/ScriptableObject.cs)

```csharp
public abstract class ScriptableObject : Asset
{
    public Guid RuntimeInstanceId { get; }
    public override bool IsValid { get; }
    public override string Name { get; set; }
    protected virtual void Awake();
    protected virtual void OnEnable();
    protected virtual void OnDisable();
    protected virtual void OnValidate();
    public static T CreateInstance<T>()
        where T : ScriptableObject;
    public static T Instantiate<T>(T source)
        where T : ScriptableObject;
}
```

## Keire.SerializableTypeAttribute

Sources: [SerializationAttributes.cs](../../KeireManaged/SerializationAttributes.cs)

```csharp
[AttributeUsage(AttributeTargets.Class | AttributeTargets.Struct)]
public sealed class SerializableTypeAttribute : Attribute
{
}
```

## Keire.SerializeFieldAttribute

Sources: [SerializationAttributes.cs](../../KeireManaged/SerializationAttributes.cs)

```csharp
[AttributeUsage(AttributeTargets.Field | AttributeTargets.Property)]
public sealed class SerializeFieldAttribute : Attribute
{
}
```

## Keire.SerializeReferenceAttribute

Sources: [SerializationAttributes.cs](../../KeireManaged/SerializationAttributes.cs)

```csharp
[AttributeUsage(AttributeTargets.Field, Inherited = true)]
public sealed class SerializeReferenceAttribute : Attribute
{
}
```

## Keire.Shader

Sources: [Rendering.cs](../../KeireManaged/Rendering.cs)

```csharp
[StableAssetTypeId("4b454952-4553-4841-4445-520000000001")]
public sealed class Shader : Asset
{
}
```

## Keire.ShaderFunction

Sources: [NativeAssets.cs](../../KeireManaged/NativeAssets.cs)

```csharp
[StableAssetTypeId("4b454952-4553-4655-4e43-54494f4e0001")]
public sealed class ShaderFunction : Asset
{
}
```

## Keire.ShaderGraph

Sources: [Rendering.cs](../../KeireManaged/Rendering.cs)

```csharp
[StableAssetTypeId("4b454952-4553-4752-4150-480000000001")]
public sealed class ShaderGraph : Asset
{
}
```

## Keire.ShaderGraphInstance

Sources: [Rendering.cs](../../KeireManaged/Rendering.cs)

```csharp
[StableAssetTypeId("4b454952-4553-4749-4e53-540000000001")]
public sealed class ShaderGraphInstance : Asset
{
}
```

## Keire.ShadowQuality

Sources: [Rendering.cs](../../KeireManaged/Rendering.cs)

```csharp
public enum ShadowQuality
{
    Disabled,
    Hard,
    Soft
}
```

## Keire.ShadowResolution

Sources: [Rendering.cs](../../KeireManaged/Rendering.cs)

```csharp
public enum ShadowResolution
{
    Low,
    Medium,
    High,
    VeryHigh
}
```

## Keire.Skeleton

Sources: [NativeAssets.cs](../../KeireManaged/NativeAssets.cs)

```csharp
[StableAssetTypeId("4b454952-4553-4b45-4c45-544f4e000001")]
public sealed class Skeleton : Asset
{
}
```

## Keire.SkinnedMesh

Sources: [NativeAssets.cs](../../KeireManaged/NativeAssets.cs)

```csharp
[StableAssetTypeId("4b454952-4553-4b49-4e4d-455348000001")]
public sealed class SkinnedMesh : Asset
{
}
```

## Keire.SpotLight

Sources: [Rendering.cs](../../KeireManaged/Rendering.cs)

```csharp
[StableComponentId("4b454952-4553-504f-544c-494748540001")]
public sealed class SpotLight : Component
{
    public Color Color { get; set; }
    public float Intensity { get; set; }
    public float Range { get; set; }
    public float InnerAngle { get; set; }
    public float OuterAngle { get; set; }
    public ShadowQuality Shadows { get; set; }
    public float ShadowStrength { get; set; }
    public float ShadowBias { get; set; }
    public LightBakeMode BakeMode { get; set; }
    public ShadowResolution ShadowResolution { get; set; }
    public Texture? Cookie { get; set; }
    public bool ContactShadows { get; set; }
    public float IndirectMultiplier { get; set; }
    public Vector2 CookieScale { get; set; }
    public Vector2 CookieOffset { get; set; }
    public float CookieRotation { get; set; }
}
```

## Keire.SpringJoint

Sources: [BuiltInComponents.cs](../../KeireManaged/BuiltInComponents.cs)

```csharp
[StableComponentId("4b454952-4553-5052-494e-474a4f494e01")]
public sealed class SpringJoint : Joint
{
    public float RestLength { get; set; }
    public float Stiffness { get; set; }
    public float Damping { get; set; }
}
```

## Keire.StableAssetTypeIdAttribute

Sources: [SerializationAttributes.cs](../../KeireManaged/SerializationAttributes.cs)

```csharp
[AttributeUsage(AttributeTargets.Class, Inherited = false)]
public sealed class StableAssetTypeIdAttribute : Attribute
{
    public StableAssetTypeIdAttribute(string id);
    public Guid Id { get; }
    public readonly ulong High;
    public readonly ulong Low;
}
```

## Keire.StableComponentIdAttribute

Sources: [SerializationAttributes.cs](../../KeireManaged/SerializationAttributes.cs)

```csharp
[AttributeUsage(AttributeTargets.Class, Inherited = false)]
public sealed class StableComponentIdAttribute : Attribute
{
    public StableComponentIdAttribute(string id);
    public Guid Id { get; }
    public readonly ulong High;
    public readonly ulong Low;
}
```

## Keire.StableFieldIdAttribute

Sources: [SerializationAttributes.cs](../../KeireManaged/SerializationAttributes.cs)

```csharp
[AttributeUsage(AttributeTargets.Field | AttributeTargets.Property)]
public sealed class StableFieldIdAttribute : Attribute
{
    public StableFieldIdAttribute(string id);
    public Guid Id { get; }
    public readonly ulong High;
    public readonly ulong Low;
}
```

## Keire.StableSerializedTypeIdAttribute

Sources: [SerializationAttributes.cs](../../KeireManaged/SerializationAttributes.cs)

```csharp
[AttributeUsage(AttributeTargets.Class, Inherited = false)]
public sealed class StableSerializedTypeIdAttribute : Attribute
{
    public StableSerializedTypeIdAttribute(string id);
    public Guid Id { get; }
    public readonly ulong High;
    public readonly ulong Low;
}
```

## Keire.TextAsset

Sources: [NativeAssets.cs](../../KeireManaged/NativeAssets.cs)

```csharp
[StableAssetTypeId("4b454952-4554-4558-5441-535345540001")]
public sealed class TextAsset : Asset
{
}
```

## Keire.Texture

Sources: [Rendering.cs](../../KeireManaged/Rendering.cs)

```csharp
[StableAssetTypeId("4b454952-4554-4558-5455-524532440001")]
public sealed class Texture : Asset
{
}
```

## Keire.Time

Sources: [RuntimeApi.cs](../../KeireManaged/RuntimeApi.cs), [RuntimeFoundation.cs](../../KeireManaged/RuntimeFoundation.cs)

```csharp
public static partial class Time
{
    public static float DeltaTime { get; }
    public static float FixedDeltaTime { get; }
    public static float UnscaledDeltaTime { get; }
    public static double Elapsed { get; }
}
public static partial class Time
{
    public static float TimeScale { get; set; }
    public static bool Paused { get; set; }
}
```

## Keire.TooltipAttribute

Sources: [SerializationAttributes.cs](../../KeireManaged/SerializationAttributes.cs)

```csharp
[AttributeUsage(AttributeTargets.Field | AttributeTargets.Property)]
public sealed class TooltipAttribute : Attribute
{
    public TooltipAttribute(string text);
    public readonly string Text;
}
```

## Keire.Transform

Sources: [Handles.cs](../../KeireManaged/Handles.cs)

```csharp
[StableComponentId("4b454952-4554-5241-4e53-464f524d0001")]
public sealed class Transform : Component
{
    public Vector3 LocalPosition { get; set; }
    public Quaternion LocalRotation { get; set; }
    public Vector3 LocalScale { get; set; }
    public Vector3 Position { get; set; }
    public Quaternion Rotation { get; set; }
    public Vector3 PresentationPosition { get; }
    public Quaternion PresentationRotation { get; }
    public bool FixedPresentationInterpolation { get; set; }
    public Vector3 Forward { get; }
    public Vector3 Right { get; }
    public Vector3 Up { get; }
    public void Translate(Vector3 translation, bool worldSpace = false);
    public void Rotate(Quaternion rotation, bool worldSpace = false);
    public void ResetPresentationInterpolation();
}
```

## Keire.UI.Align

Sources: [UiToolkit.cs](../../KeireManaged/UiToolkit.cs)

```csharp
public enum Align : byte
{
    Auto,
    FlexStart,
    Center,
    FlexEnd,
    Stretch
}
```

## Keire.UI.BackgroundFit

Sources: [UiToolkit.cs](../../KeireManaged/UiToolkit.cs)

```csharp
public enum BackgroundFit : byte
{
    Stretch,
    Contain,
    Cover,
    None
}
```

## Keire.UI.BackgroundRepeat

Sources: [UiToolkit.cs](../../KeireManaged/UiToolkit.cs)

```csharp
public enum BackgroundRepeat : byte
{
    NoRepeat,
    Repeat,
    RepeatX,
    RepeatY
}
```

## Keire.UI.BaseField of T

Sources: [UiToolkitControls.cs](../../KeireManaged/UiToolkitControls.cs)

```csharp
public abstract class BaseField<T> : BindableElement
{
    protected BaseField();
    public T Value { get; set; }
    public void SetValueWithoutNotify(T value);
    protected virtual T Normalize(T value);
}
```

## Keire.UI.BindableElement

Sources: [UiToolkitControls.cs](../../KeireManaged/UiToolkitControls.cs)

```csharp
[UxmlElement]
public class BindableElement : VisualElement
{
    [UxmlAttribute("binding-path")]
    public string BindingPath { get; set; }
}
```

## Keire.UI.BindingDiagnostic

Sources: [UiToolkit.cs](../../KeireManaged/UiToolkit.cs)

```csharp
public sealed record BindingDiagnostic(string TargetProperty, string SourcePath, Type SourceType, Type TargetType, string Message)
{
}
```

## Keire.UI.BindingMode

Sources: [UiToolkit.cs](../../KeireManaged/UiToolkit.cs)

```csharp
public enum BindingMode : byte
{
    OneWay,
    TwoWay,
    OneTime
}
```

## Keire.UI.BoxShadow

Sources: [UiToolkit.cs](../../KeireManaged/UiToolkit.cs)

```csharp
public readonly record struct BoxShadow(Keire.Vector2 Offset, float BlurRadius, float SpreadRadius, Keire.Color Color, bool Inset = false)
{
}
```

## Keire.UI.Button

Sources: [UiToolkitControls.cs](../../KeireManaged/UiToolkitControls.cs)

```csharp
[UxmlElement]
public class Button : TextElement
{
    public Button();
    public Button(Action clicked);
    public event Action? Clicked;
    public void Click();
}
```

## Keire.UI.ChangeEvent of T

Sources: [UiToolkit.cs](../../KeireManaged/UiToolkit.cs)

```csharp
public sealed class ChangeEvent<T> : EventBase
{
    public required T PreviousValue { get; init; }
    public required T NewValue { get; init; }
}
```

## Keire.UI.ClickEvent

Sources: [UiToolkit.cs](../../KeireManaged/UiToolkit.cs)

```csharp
public sealed class ClickEvent : PointerEventBase
{
}
```

## Keire.UI.DataBinding

Sources: [UiToolkit.cs](../../KeireManaged/UiToolkit.cs)

```csharp
public sealed class DataBinding
{
    public required string SourcePath { get; init; }
    public BindingMode Mode { get; init; }
    public Func<object?, object?>? ToTarget { get; init; }
    public Func<object?, object?>? ToSource { get; init; }
}
```

## Keire.UI.DisplayStyle

Sources: [UiToolkit.cs](../../KeireManaged/UiToolkit.cs)

```csharp
public enum DisplayStyle : byte
{
    Flex,
    None
}
```

## Keire.UI.DropdownField

Sources: [UiToolkitControls.cs](../../KeireManaged/UiToolkitControls.cs)

```csharp
[UxmlElement]
public sealed class DropdownField : BaseField<string>
{
    public IReadOnlyList<string> Choices { get; set; }
    protected override string Normalize(string value);
}
```

## Keire.UI.EventBase

Sources: [UiToolkit.cs](../../KeireManaged/UiToolkit.cs)

```csharp
public abstract class EventBase
{
    public VisualElement? Target { get; internal set; }
    public VisualElement? CurrentTarget { get; internal set; }
    public PropagationPhase PropagationPhase { get; internal set; }
    public bool IsPropagationStopped { get; private set; }
    public bool IsImmediatePropagationStopped { get; private set; }
    public bool IsDefaultPrevented { get; private set; }
    public virtual bool Bubbles { get; }
    public virtual bool TricklesDown { get; }
    public void StopPropagation();
    public void StopImmediatePropagation();
    public void PreventDefault();
}
```

## Keire.UI.FlexDirection

Sources: [UiToolkit.cs](../../KeireManaged/UiToolkit.cs)

```csharp
public enum FlexDirection : byte
{
    Column,
    ColumnReverse,
    Row,
    RowReverse
}
```

## Keire.UI.FocusInEvent

Sources: [UiToolkit.cs](../../KeireManaged/UiToolkit.cs)

```csharp
public sealed class FocusInEvent : EventBase
{
}
```

## Keire.UI.FocusOutEvent

Sources: [UiToolkit.cs](../../KeireManaged/UiToolkit.cs)

```csharp
public sealed class FocusOutEvent : EventBase
{
}
```

## Keire.UI.Foldout

Sources: [UiToolkitControls.cs](../../KeireManaged/UiToolkitControls.cs)

```csharp
[UxmlElement]
public sealed class Foldout : Toggle
{
    [UxmlAttribute]
    public string Text { get; set; }
}
```

## Keire.UI.FontFamily

Sources: [UiToolkit.cs](../../KeireManaged/UiToolkit.cs)

```csharp
public sealed class FontFamily : Keire.Asset
{
}
```

## Keire.UI.FontSlant

Sources: [UiToolkit.cs](../../KeireManaged/UiToolkit.cs)

```csharp
public enum FontSlant : byte
{
    Normal,
    Italic,
    Oblique
}
```

## Keire.UI.ICollectionVirtualizationController

Sources: [UiToolkitControls.cs](../../KeireManaged/UiToolkitControls.cs)

```csharp
public interface ICollectionVirtualizationController
{
    int FirstVisibleIndex { get; }
    int VisibleCount { get; }
    void SetViewport(int firstVisibleIndex, int visibleCount);
}
```

## Keire.UI.Image

Sources: [UiToolkitControls.cs](../../KeireManaged/UiToolkitControls.cs)

```csharp
[UxmlElement]
public class Image : VisualElement
{
    [UxmlAttribute]
    public Keire.Texture? Source { get; set; }
    [UxmlAttribute]
    public Keire.Color Tint { get; set; }
}
```

## Keire.UI.Justify

Sources: [UiToolkit.cs](../../KeireManaged/UiToolkit.cs)

```csharp
public enum Justify : byte
{
    FlexStart,
    Center,
    FlexEnd,
    SpaceBetween,
    SpaceAround,
    SpaceEvenly
}
```

## Keire.UI.KeyDownEvent

Sources: [UiToolkit.cs](../../KeireManaged/UiToolkit.cs)

```csharp
public sealed class KeyDownEvent : EventBase
{
    public required string Key { get; init; }
    public bool ShiftKey { get; init; }
    public bool CtrlKey { get; init; }
    public bool AltKey { get; init; }
}
```

## Keire.UI.Label

Sources: [UiToolkitControls.cs](../../KeireManaged/UiToolkitControls.cs)

```csharp
[UxmlElement]
public sealed class Label : TextElement
{
    public Label();
    public Label(string text);
}
```

## Keire.UI.ListView

Sources: [UiToolkitControls.cs](../../KeireManaged/UiToolkitControls.cs)

```csharp
[UxmlElement]
public class ListView : ScrollView, ICollectionVirtualizationController
{
    public IReadOnlyList<object?> ItemsSource { get; set; }
    public Func<VisualElement> MakeItem { get; set; }
    public Action<VisualElement, int>? BindItem { get; set; }
    public int Overscan { get; set; }
    public int FirstVisibleIndex { get; }
    public int VisibleCount { get; }
    public IReadOnlyList<VisualElement> RealizedItems { get; }
    public void SetViewport(int firstVisibleIndex, int visibleCount);
    public void RefreshItems();
}
```

## Keire.UI.NavigationDirection

Sources: [UiToolkit.cs](../../KeireManaged/UiToolkit.cs)

```csharp
public enum NavigationDirection : byte
{
    Previous,
    Next,
    Left,
    Right,
    Up,
    Down
}
```

## Keire.UI.NavigationMoveEvent

Sources: [UiToolkit.cs](../../KeireManaged/UiToolkit.cs)

```csharp
public sealed class NavigationMoveEvent : EventBase
{
    public NavigationDirection Direction { get; init; }
}
```

## Keire.UI.Overflow

Sources: [UiToolkit.cs](../../KeireManaged/UiToolkit.cs)

```csharp
public enum Overflow : byte
{
    Visible,
    Hidden,
    Scroll
}
```

## Keire.UI.PanelSettings

Sources: [UiToolkit.cs](../../KeireManaged/UiToolkit.cs)

```csharp
[Keire.StableAssetTypeId("4b454952-4555-4950-414e-454c00000001")]
public sealed class PanelSettings : Keire.Asset
{
}
```

## Keire.UI.PointerDownEvent

Sources: [UiToolkit.cs](../../KeireManaged/UiToolkit.cs)

```csharp
public sealed class PointerDownEvent : PointerEventBase
{
}
```

## Keire.UI.PointerEventBase

Sources: [UiToolkit.cs](../../KeireManaged/UiToolkit.cs)

```csharp
public abstract class PointerEventBase : EventBase
{
    public int PointerId { get; init; }
    public int Button { get; init; }
    public Keire.Vector2 Position { get; init; }
}
```

## Keire.UI.PointerMoveEvent

Sources: [UiToolkit.cs](../../KeireManaged/UiToolkit.cs)

```csharp
public sealed class PointerMoveEvent : PointerEventBase
{
}
```

## Keire.UI.PointerUpEvent

Sources: [UiToolkit.cs](../../KeireManaged/UiToolkit.cs)

```csharp
public sealed class PointerUpEvent : PointerEventBase
{
}
```

## Keire.UI.Position

Sources: [UiToolkit.cs](../../KeireManaged/UiToolkit.cs)

```csharp
public enum Position : byte
{
    Relative,
    Absolute
}
```

## Keire.UI.ProgressBar

Sources: [UiToolkitControls.cs](../../KeireManaged/UiToolkitControls.cs)

```csharp
[UxmlElement]
public sealed class ProgressBar : VisualElement
{
    [UxmlAttribute]
    public float LowValue { get; set; }
    [UxmlAttribute]
    public float HighValue { get; set; }
    [UxmlAttribute]
    public string Title { get; set; }
    public float Value { get; set; }
}
```

## Keire.UI.PropagationPhase

Sources: [UiToolkit.cs](../../KeireManaged/UiToolkit.cs)

```csharp
public enum PropagationPhase : byte
{
    None,
    TrickleDown,
    AtTarget,
    BubbleUp
}
```

## Keire.UI.RuntimeVisualElement

Sources: [UiToolkit.cs](../../KeireManaged/UiToolkit.cs)

```csharp
public sealed class RuntimeVisualElement
{
    public Keire.AssetId StableId { get; }
    public RuntimeVisualElementType Type { get; }
    public bool IsAlive { get; }
    public string Text { get; set; }
    public float Value { get; set; }
    public bool Checked { get; set; }
    public bool Interactable { get; set; }
    public bool Enabled { get; set; }
    public bool HasFocus { get; }
    public bool ClickedThisFrame { get; }
    public bool ChangedThisFrame { get; }
    public bool SubmittedThisFrame { get; }
    public bool CancelledThisFrame { get; }
    public void Focus();
}
```

## Keire.UI.RuntimeVisualElementType

Sources: [UiToolkit.cs](../../KeireManaged/UiToolkit.cs)

```csharp
public enum RuntimeVisualElementType : byte
{
    Canvas,
    Panel,
    Text,
    Image,
    Button,
    HorizontalLayout,
    VerticalLayout,
    Spacer,
    GridLayout,
    Slider,
    Toggle,
    InputField,
    ScrollView
}
```

## Keire.UI.ScrollView

Sources: [UiToolkitControls.cs](../../KeireManaged/UiToolkitControls.cs)

```csharp
[UxmlElement]
public class ScrollView : VisualElement
{
    public Keire.Vector2 ScrollOffset { get; set; }
}
```

## Keire.UI.Slider

Sources: [UiToolkitControls.cs](../../KeireManaged/UiToolkitControls.cs)

```csharp
[UxmlElement]
public sealed class Slider : BaseField<float>
{
    [UxmlAttribute]
    public float LowValue { get; set; }
    [UxmlAttribute]
    public float HighValue { get; set; }
    [UxmlAttribute]
    public float Step { get; set; }
    protected override float Normalize(float value);
}
```

## Keire.UI.Style

Sources: [UiToolkit.cs](../../KeireManaged/UiToolkit.cs)

```csharp
public sealed class Style
{
    public DisplayStyle Display { get; set; }
    public Position Position { get; set; }
    public FlexDirection FlexDirection { get; set; }
    public Wrap FlexWrap { get; set; }
    public Justify JustifyContent { get; set; }
    public Align AlignItems { get; set; }
    public Align AlignSelf { get; set; }
    public Overflow Overflow { get; set; }
    public float? Width { get; set; }
    public float? Height { get; set; }
    public float? MinWidth { get; set; }
    public float? MinHeight { get; set; }
    public float? MaxWidth { get; set; }
    public float? MaxHeight { get; set; }
    public float FlexGrow { get; set; }
    public float FlexShrink { get; set; }
    public float Gap { get; set; }
    public float Opacity { get; set; }
    public Keire.Color Color { get; set; }
    public Keire.Color BackgroundColor { get; set; }
    public Keire.Texture? BackgroundImage { get; set; }
    public Keire.Color BackgroundTint { get; set; }
    public BackgroundFit BackgroundFit { get; set; }
    public BackgroundRepeat BackgroundRepeat { get; set; }
    public Keire.Vector2 BackgroundPosition { get; set; }
    public StyleEdges<float> BackgroundSlice { get; set; }
    public StyleEdges<float> BorderWidths { get; set; }
    public StyleEdges<Keire.Color> BorderColors { get; set; }
    public StyleCorners<float> BorderRadii { get; set; }
    public IReadOnlyList<BoxShadow> BoxShadows { get; set; }
    public IReadOnlyList<BoxShadow> TextShadows { get; set; }
    public Keire.Texture? AlphaMask { get; set; }
    public Keire.Vector2 Translate { get; set; }
    public Keire.Vector2 Scale { get; set; }
    public float RotationDegrees { get; set; }
    public Keire.Vector2 TransformOrigin { get; set; }
    public FontFamily? FontFamily { get; set; }
    public ushort FontWeight { get; set; }
    public FontSlant FontSlant { get; set; }
    public float FontSize { get; set; }
    public float? LineHeight { get; set; }
    public float LetterSpacing { get; set; }
    public float WordSpacing { get; set; }
    public TextWrap TextWrap { get; set; }
    public TextOverflow TextOverflow { get; set; }
    public TextDirection TextDirection { get; set; }
    public string Language { get; set; }
    public ushort MaximumLines { get; set; }
    public IReadOnlyList<float> TransitionDelays { get; set; }
    public IReadOnlyList<TransitionEasing> TransitionEasings { get; set; }
}
```

## Keire.UI.StyleCorners of T

Sources: [UiToolkit.cs](../../KeireManaged/UiToolkit.cs)

```csharp
public readonly record struct StyleCorners<T>(T TopLeft, T TopRight, T BottomRight, T BottomLeft)
{
}
```

## Keire.UI.StyleEdges of T

Sources: [UiToolkit.cs](../../KeireManaged/UiToolkit.cs)

```csharp
public readonly record struct StyleEdges<T>(T Left, T Top, T Right, T Bottom)
{
}
```

## Keire.UI.StyleSheet

Sources: [UiToolkit.cs](../../KeireManaged/UiToolkit.cs)

```csharp
[Keire.StableAssetTypeId("4b454952-4555-4953-5459-4c4500000001")]
public sealed class StyleSheet : Keire.Asset
{
}
```

## Keire.UI.SubmitEvent

Sources: [UiToolkit.cs](../../KeireManaged/UiToolkit.cs)

```csharp
public sealed class SubmitEvent : EventBase
{
}
```

## Keire.UI.TabView

Sources: [UiToolkitControls.cs](../../KeireManaged/UiToolkitControls.cs)

```csharp
[UxmlElement]
public sealed class TabView : VisualElement
{
    public int SelectedIndex { get; set; }
}
```

## Keire.UI.TemplateContainer

Sources: [UiToolkitControls.cs](../../KeireManaged/UiToolkitControls.cs)

```csharp
[UxmlElement]
public sealed class TemplateContainer : VisualElement
{
    [UxmlAttribute]
    public string TemplateName { get; set; }
}
```

## Keire.UI.TextDirection

Sources: [UiToolkit.cs](../../KeireManaged/UiToolkit.cs)

```csharp
public enum TextDirection : byte
{
    Automatic,
    LeftToRight,
    RightToLeft
}
```

## Keire.UI.TextElement

Sources: [UiToolkitControls.cs](../../KeireManaged/UiToolkitControls.cs)

```csharp
[UxmlElement]
public class TextElement : VisualElement
{
    [UxmlAttribute]
    public string Text { get; set; }
}
```

## Keire.UI.TextField

Sources: [UiToolkitControls.cs](../../KeireManaged/UiToolkitControls.cs)

```csharp
[UxmlElement]
public sealed class TextField : BaseField<string>
{
    [UxmlAttribute]
    public bool Multiline { get; set; }
    [UxmlAttribute]
    public bool IsPasswordField { get; set; }
    [UxmlAttribute]
    public int MaxLength { get; set; }
    protected override string Normalize(string value);
}
```

## Keire.UI.TextInputEvent

Sources: [UiToolkit.cs](../../KeireManaged/UiToolkit.cs)

```csharp
public sealed class TextInputEvent : EventBase
{
    public required string Text { get; init; }
}
```

## Keire.UI.TextOverflow

Sources: [UiToolkit.cs](../../KeireManaged/UiToolkit.cs)

```csharp
public enum TextOverflow : byte
{
    Clip,
    Ellipsis
}
```

## Keire.UI.TextWrap

Sources: [UiToolkit.cs](../../KeireManaged/UiToolkit.cs)

```csharp
public enum TextWrap : byte
{
    Normal,
    NoWrap
}
```

## Keire.UI.Toggle

Sources: [UiToolkitControls.cs](../../KeireManaged/UiToolkitControls.cs)

```csharp
[UxmlElement]
public class Toggle : BaseField<bool>
{
    [UxmlAttribute]
    public string Label { get; set; }
}
```

## Keire.UI.Toolbar

Sources: [UiToolkitControls.cs](../../KeireManaged/UiToolkitControls.cs)

```csharp
[UxmlElement]
public sealed class Toolbar : VisualElement
{
}
```

## Keire.UI.TransitionEasing

Sources: [UiToolkit.cs](../../KeireManaged/UiToolkit.cs)

```csharp
public enum TransitionEasing : byte
{
    Linear,
    Ease,
    EaseIn,
    EaseOut,
    EaseInOut
}
```

## Keire.UI.TreeView

Sources: [UiToolkitControls.cs](../../KeireManaged/UiToolkitControls.cs)

```csharp
[UxmlElement]
public sealed class TreeView : ListView
{
}
```

## Keire.UI.TrickleDown

Sources: [UiToolkit.cs](../../KeireManaged/UiToolkit.cs)

```csharp
public enum TrickleDown : byte
{
    NoTrickleDown,
    TrickleDown
}
```

## Keire.UI.UIDocument

Sources: [UiToolkit.cs](../../KeireManaged/UiToolkit.cs)

```csharp
[Keire.StableComponentId("4b454952-4555-4944-4f43-554d454e5401")]
public sealed class UIDocument : Keire.Component
{
    public VisualTreeAsset? VisualTreeAsset { get; set; }
    public PanelSettings? PanelSettings { get; set; }
    public Keire.Material? Material { get; set; }
    public int SortingOrder { get; set; }
    public bool ReceivesInput { get; set; }
    public RuntimeVisualElement? RootVisualElement { get; }
    public RuntimeVisualElement? Q(string name);
    public RuntimeVisualElement? Q(Keire.AssetId stableId);
    public void SetBindingValue(string path, string value);
    public void SetBindingValue(string path, float value);
    public void SetBindingValue(string path, bool value);
    public bool TryGetBindingValue(string path, out string value);
    public bool TryGetBindingValue(string path, out float value);
    public bool TryGetBindingValue(string path, out bool value);
    public bool ClearBindingSource();
}
```

## Keire.UI.UQueryBuilder of T

Sources: [UiToolkit.cs](../../KeireManaged/UiToolkit.cs)

```csharp
public readonly struct UQueryBuilder<T>
    where T : VisualElement
{
    public IEnumerable<T> ToList();
    public T? FirstOrDefault();
    public void ForEach(Action<T> action);
}
```

## Keire.UI.UxmlAttributeAttribute

Sources: [UiToolkit.cs](../../KeireManaged/UiToolkit.cs)

```csharp
[AttributeUsage(AttributeTargets.Property, Inherited = true)]
public sealed class UxmlAttributeAttribute : Attribute
{
    public UxmlAttributeAttribute(string name = "");
    public string Name { get; }
}
```

## Keire.UI.UxmlAttributeDescriptor

Sources: [UiToolkit.cs](../../KeireManaged/UiToolkit.cs)

```csharp
public sealed record UxmlAttributeDescriptor(string Name, string Property, Type ValueType)
{
}
```

## Keire.UI.UxmlElementAttribute

Sources: [UiToolkit.cs](../../KeireManaged/UiToolkit.cs)

```csharp
[AttributeUsage(AttributeTargets.Class, Inherited = false)]
public sealed class UxmlElementAttribute : Attribute
{
    public UxmlElementAttribute(string name = "");
    public string Name { get; }
}
```

## Keire.UI.UxmlElementDescriptor

Sources: [UiToolkit.cs](../../KeireManaged/UiToolkit.cs)

```csharp
public sealed record UxmlElementDescriptor(string Name, Type ElementType, IReadOnlyList<UxmlAttributeDescriptor> Attributes, long Generation)
{
}
```

## Keire.UI.UxmlElementRegistry

Sources: [UiToolkit.cs](../../KeireManaged/UiToolkit.cs)

```csharp
public static class UxmlElementRegistry
{
    public static long Generation { get; }
    public static UxmlElementDescriptor Register<T>()
        where T : VisualElement, new();
    public static VisualElement Create(string name);
    public static IReadOnlyList<UxmlElementDescriptor> Snapshot();
}
```

## Keire.UI.VisualElement

Sources: [UiToolkit.cs](../../KeireManaged/UiToolkit.cs)

```csharp
[UxmlElement]
public class VisualElement
{
    public VisualElement();
    public Guid StableId { get; internal set; }
    [UxmlAttribute]
    public string Name { get; set; }
    public VisualElement? Parent { get; private set; }
    public IReadOnlyList<VisualElement> Children { get; }
    public IReadOnlyCollection<string> ClassList { get; }
    public Style Style { get; }
    public bool EnabledSelf { get; private set; }
    public bool EnabledInHierarchy { get; }
    public bool HasFocus { get; }
    public bool Focusable { get; set; }
    public int TabIndex { get; set; }
    public string Tooltip { get; set; }
    public object? UserData { get; set; }
    public BindingDiagnostic? LastBindingDiagnostic { get; private set; }
    public object? DataSource { get; set; }
    public void Add(VisualElement child);
    public void Insert(int index, VisualElement child);
    public bool Remove(VisualElement child);
    public void RemoveFromHierarchy();
    public void Clear();
    public void AddToClassList(string className);
    public bool RemoveFromClassList(string className);
    public bool ClassListContains(string className);
    public void EnableInClassList(string className, bool enable);
    public void SetEnabled(bool enabled);
    public void Focus();
    public void Blur();
    public void CapturePointer(int pointerId);
    public void ReleasePointer(int pointerId);
    public bool HasPointerCapture(int pointerId);
    public VisualElement? GetCapturingElement(int pointerId);
    public T? Q<T>(string? name = null, string? className = null)
        where T : VisualElement;
    public UQueryBuilder<T> Query<T>(string? name = null, string? className = null)
        where T : VisualElement;
    public void RegisterCallback<TEvent>(Action<TEvent> callback, TrickleDown trickleDown = TrickleDown.NoTrickleDown)
        where TEvent : EventBase;
    public void UnregisterCallback<TEvent>(Action<TEvent> callback, TrickleDown trickleDown = TrickleDown.NoTrickleDown)
        where TEvent : EventBase;
    public void SendEvent(EventBase evt);
    public void SetBinding(string targetProperty, DataBinding binding);
    public bool ClearBinding(string targetProperty);
    public void UpdateBindings();
    protected void WriteBackBinding(string targetProperty, object? value);
}
```

## Keire.UI.VisualTreeAsset

Sources: [UiToolkit.cs](../../KeireManaged/UiToolkit.cs)

```csharp
[Keire.StableAssetTypeId("4b454952-4555-4954-5245-450000000001")]
public sealed class VisualTreeAsset : Keire.Asset
{
}
```

## Keire.UI.Wrap

Sources: [UiToolkit.cs](../../KeireManaged/UiToolkit.cs)

```csharp
public enum Wrap : byte
{
    NoWrap,
    Wrap,
    WrapReverse
}
```

## Keire.Vector2

Sources: [MathTypes.cs](../../KeireManaged/MathTypes.cs)

```csharp
public readonly record struct Vector2(float X, float Y)
{
    public static Vector2 Zero { get; }
    public static Vector2 One { get; }
    public float LengthSquared { get; }
    public float Length { get; }
    public Vector2 Normalized { get; }
    public static Vector2 operator +(Vector2 left, Vector2 right);
    public static Vector2 operator -(Vector2 left, Vector2 right);
    public static Vector2 operator -(Vector2 value);
    public static Vector2 operator *(Vector2 value, float scale);
    public static Vector2 operator *(float scale, Vector2 value);
    public static Vector2 operator /(Vector2 value, float scale);
    public override string ToString();
}
```

## Keire.Vector2Control

Sources: [DirectInput.cs](../../KeireManaged/DirectInput.cs)

```csharp
public sealed class Vector2Control : InputControl
{
    public Vector2 ReadValue();
}
```

## Keire.Vector3

Sources: [MathTypes.cs](../../KeireManaged/MathTypes.cs)

```csharp
public readonly record struct Vector3(float X, float Y, float Z)
{
    public static Vector3 Zero { get; }
    public static Vector3 One { get; }
    public static Vector3 Up { get; }
    public static Vector3 Forward { get; }
    public static Vector3 Right { get; }
    public float LengthSquared { get; }
    public float Length { get; }
    public Vector3 Normalized { get; }
    public static float Dot(Vector3 left, Vector3 right);
    public static Vector3 Cross(Vector3 left, Vector3 right);
    public static Vector3 Reflect(Vector3 direction, Vector3 normal);
    public static Vector3 Lerp(Vector3 from, Vector3 to, float amount);
    public static Vector3 operator +(Vector3 left, Vector3 right);
    public static Vector3 operator -(Vector3 left, Vector3 right);
    public static Vector3 operator -(Vector3 value);
    public static Vector3 operator *(Vector3 value, float scale);
    public static Vector3 operator *(float scale, Vector3 value);
    public static Vector3 operator /(Vector3 value, float scale);
    public override string ToString();
}
```

## Keire.Vector4

Sources: [MathTypes.cs](../../KeireManaged/MathTypes.cs)

```csharp
public readonly record struct Vector4(float X, float Y, float Z, float W)
{
    public override string ToString();
}
```

## Keire.Vfx

Sources: [RuntimeApi.cs](../../KeireManaged/RuntimeApi.cs)

```csharp
public static class Vfx
{
    public static VfxEmitter? Play(Entity entity, VfxEffect effect, bool restart = false);
    public static VfxEmitter? Play(Entity entity, AssetId effect, bool restart = false);
    public static bool Stop(Entity entity);
    public static bool Pause(Entity entity);
    public static bool Resume(Entity entity);
    public static bool IsAlive(Entity entity);
    public static bool SendEvent(Entity entity, string eventName, uint spawnCount = 1);
    public static bool SetParameter(Entity entity, AssetId parameter, VfxRange<float> value);
    public static bool SetParameter(Entity entity, AssetId parameter, VfxRange<long> value);
    public static bool SetParameter(Entity entity, AssetId parameter, VfxRange<ulong> value);
    public static bool SetParameter(Entity entity, AssetId parameter, VfxRange<Vector2> value);
    public static bool SetParameter(Entity entity, AssetId parameter, VfxRange<Vector3> value);
    public static bool SetParameter(Entity entity, AssetId parameter, VfxRange<Vector4> value);
    public static bool SetParameter(Entity entity, AssetId parameter, VfxRange<Color> value);
}
```

## Keire.VfxEffect

Sources: [RuntimeApi.cs](../../KeireManaged/RuntimeApi.cs)

```csharp
[StableAssetTypeId("4b454952-4556-4658-4546-464543540001")]
public sealed class VfxEffect : Asset
{
}
```

## Keire.VfxEmitter

Sources: [RuntimeApi.cs](../../KeireManaged/RuntimeApi.cs)

```csharp
[StableComponentId("4b454952-4556-4658-454d-495454455201")]
public sealed class VfxEmitter : Component
{
    public bool IsAlive { get; }
    public bool SendEvent(string eventName, uint spawnCount = 1);
    public bool Pause();
    public bool Resume();
    public bool Stop();
    public bool Restart(AssetId effect);
    public bool Restart(VfxEffect effect);
    public bool SetParameter(AssetId parameter, VfxRange<float> value);
    public bool SetParameter(AssetId parameter, VfxRange<long> value);
    public bool SetParameter(AssetId parameter, VfxRange<ulong> value);
    public bool SetParameter(AssetId parameter, VfxRange<Vector2> value);
    public bool SetParameter(AssetId parameter, VfxRange<Vector3> value);
    public bool SetParameter(AssetId parameter, VfxRange<Vector4> value);
    public bool SetParameter(AssetId parameter, VfxRange<Color> value);
}
```

## Keire.VfxRange of T

Sources: [RuntimeApi.cs](../../KeireManaged/RuntimeApi.cs)

```csharp
public readonly record struct VfxRange<T>
    where T : unmanaged
{
    public VfxRange(T first, T second);
    public T Minimum { get; }
    public T Maximum { get; }
    public void Deconstruct(out T minimum, out T maximum);
}
```

## Keire.VfxSubgraph

Sources: [NativeAssets.cs](../../KeireManaged/NativeAssets.cs)

```csharp
[StableAssetTypeId("4b454952-4556-4658-5355-424752410001")]
public sealed class VfxSubgraph : Asset
{
}
```

## Keire.VfxVolume

Sources: [RuntimeApi.cs](../../KeireManaged/RuntimeApi.cs)

```csharp
[StableAssetTypeId("4b454952-4556-4658-564f-4c554d450001")]
public sealed class VfxVolume : Asset
{
}
```

## Keire.WaitForEndOfFrame

Sources: [Coroutines.cs](../../KeireManaged/Coroutines.cs)

```csharp
public sealed class WaitForEndOfFrame : YieldInstruction
{
}
```

## Keire.WaitForFixedUpdate

Sources: [Coroutines.cs](../../KeireManaged/Coroutines.cs)

```csharp
public sealed class WaitForFixedUpdate : YieldInstruction
{
}
```

## Keire.WaitForSeconds

Sources: [Coroutines.cs](../../KeireManaged/Coroutines.cs)

```csharp
public sealed class WaitForSeconds : YieldInstruction
{
    public WaitForSeconds(float seconds);
}
```

## Keire.WaitForSecondsRealtime

Sources: [Coroutines.cs](../../KeireManaged/Coroutines.cs)

```csharp
public sealed class WaitForSecondsRealtime : YieldInstruction
{
    public WaitForSecondsRealtime(float seconds);
}
```

## Keire.WaitUntil

Sources: [Coroutines.cs](../../KeireManaged/Coroutines.cs)

```csharp
public sealed class WaitUntil(Func<bool> predicate) : CustomYieldInstruction
{
    public override bool KeepWaiting { get; }
}
```

## Keire.WaitWhile

Sources: [Coroutines.cs](../../KeireManaged/Coroutines.cs)

```csharp
public sealed class WaitWhile(Func<bool> predicate) : CustomYieldInstruction
{
    public override bool KeepWaiting { get; }
}
```

## Keire.YieldInstruction

Sources: [Coroutines.cs](../../KeireManaged/Coroutines.cs)

```csharp
public abstract class YieldInstruction
{
}
```
