param(
    [int]$Frames = 4200,
    [int]$HostFrames = 0,
    [int]$ClientFrames = 0,
    [int]$WaitTimeoutMs = 240000,
    [string]$Exe = "build\debug-windows-x86_64\melonDS.exe",
    [string]$Rom = "roms\nsmb.nds",
    [string]$HostRom = "",
    [string]$ClientRom = "",
    [string]$InputScript = "tests\nsmb_mario_vs_luigi.inputs",
    [switch]$GameStateTrace,
    [int]$GameStateTraceInterval = 60,
    [int]$GameStateTraceStartFrame = 0,
    [int]$GameStateTraceEndFrame = 0,
    [switch]$GameStateTraceExtended,
    [switch]$StateSync,
    [switch]$StateApply,
    [int]$StateSyncInterval = 60,
    [switch]$StateSyncExtended,
    [string]$StateApplyMode = "",
    [int]$ScreenshotInterval = 600,
    [string]$RamDumpFrames = "",
    [int]$RamDumpInterval = 0,
    [string]$StateSaveDir = "",
    [int]$StateSaveFrame = 0,
    [string]$StateLoadDir = "",
    [int]$StateLoadFrame = -1,
    [int]$VsStarSnapFrame = 0,
    [int]$VsStarSnapPlayerSlot = 0,
    [int]$PlayerSnapToStarFrame = 0,
    [int]$PlayerSnapToStarSlot = 0,
    [int]$PlayerStickToStarStartFrame = 0,
    [int]$PlayerStickToStarEndFrame = 0,
    [int]$PlayerStickToStarSlot = 0,
    [switch]$LanMPTrace,
    [int]$LanMPTraceDumpLen = 512,
    [switch]$NoHashLog,
    [switch]$NoFrameLimit,
    [switch]$FixedFrameTime,
    [double]$TargetFps = 0.0,
    [string]$HostPacketReplayFile = "",
    [string]$ClientPacketReplayFile = "",
    [switch]$PacketCapture,
    [switch]$PacketCaptureAllowPreGame,
    [switch]$PacketBridge,
    [switch]$PacketBridgeAllowJit,
    [switch]$PacketBridgeAllowPreGame,
    [switch]$PacketBridgeTrace,
    [int]$PacketBridgePort = 8165,
    [int]$PacketBridgeStartFrame = 0,
    [string]$HostLocalInstance = "",
    [string]$ClientLocalInstance = "",
    [string]$HostPacketBridgeLocalPlayer = "",
    [string]$ClientPacketBridgeLocalPlayer = "",
    [string]$HostPacketBridgeLoadLevelPlayerID = "",
    [string]$ClientPacketBridgeLoadLevelPlayerID = "",
    [string]$HostPacketBridgeForceGameLocalPlayerID = "",
    [string]$ClientPacketBridgeForceGameLocalPlayerID = "",
    [int]$PacketBridgeForceGameLocalPlayerIDStartFrame = 0,
    [switch]$PacketBridgeForceGameLocalPlayerIDEarly,
    [int]$PacketBridgeReplayTickOffset = 0,
    [int]$HostPacketBridgeReplayTickOffset = [int]::MinValue,
    [int]$ClientPacketBridgeReplayTickOffset = [int]::MinValue,
    [switch]$PacketBridgeWait,
    [int]$PacketBridgeWaitTimeoutMs = 5,
    [int]$PacketBridgeWaitStartFrame = 0,
    [int]$PacketBridgeWaitTickAhead = 0,
    [switch]$PacketBridgeStrictRemote,
    [string]$PacketBridgeStrictPlayers = "",
    [int]$PacketBridgeStrictStartFrame = 0,
    [int]$PacketBridgeStrictRequireLead = 0,
    [int]$PacketBridgeLiveFallbackWindow = 0,
    [switch]$PacketBridgeLiveFallbackNearest,
    [switch]$PacketBridgeLiveFallbackLatestBefore,
    [int]$PacketBridgeLiveFallbackStartFrame = 0,
    [switch]$PacketBridgeReplayReturnLookupTick,
    [switch]$PacketBridgeMaintainPacketFreeBytes,
    [switch]$PacketBridgeMaintainSessionPeers,
    [int]$PacketBridgeMaintainSessionPeersStartFrame = 0,
    [switch]$PacketBridgeMaintainSessionPeersHostOnly,
    [switch]$PacketBridgeMaintainSessionPeersClientOnly,
    [switch]$PacketBridgeClientConfirmToStageStart,
    [int]$PacketBridgeClientConfirmToStageStartFrame = 0,
    [int]$PacketBridgeLowerStatusResult = -1,
    [switch]$PacketBridgeStageStartReadyProbe,
    [int]$PacketBridgeStageStartPacketAction = -1,
    [int]$PacketBridgeStageStartNet14 = -1,
    [int]$PacketBridgeStageStartNet1C = -1,
    [int]$PacketBridgeStageStartNet20 = -1,
    [int]$PacketBridgeStageStartNet20Step3 = -1,
    [int]$PacketBridgeStageStartNet20Step3MinTimer = -1,
    [switch]$PacketBridgeStageStartNet20Check,
    [int]$PacketBridgeStageStartNet20CheckMinTimer = -1,
    [switch]$PacketBridgeStageStartStep6Close,
    [int]$PacketBridgeStageStartStep6CloseMinTimer = -1,
    [switch]$PacketBridgeStageSceneReadyClose,
    [int]$PacketBridgeStageSceneReadyCloseStartFrame = -1,
    [switch]$PacketBridgeReadPacketByte,
    [switch]$PacketBridgeCheckPacketBits,
    [int]$PacketBridgeStageStartNet24 = -1,
    [int]$PacketBridgeStageStartNet2C = -1,
    [int]$PacketBridgeStageStartNet34 = -1,
    [switch]$StageStartDispatchTrace,
    [switch]$StageStartDispatchTraceFull,
    [int]$StageStartDispatchTraceStartFrame = 0,
    [int]$StageStartDispatchTraceEndFrame = 0,
    [string]$PacketBridgeReplayOps = "",
    [switch]$PacketBridgeDirectCapture,
    [switch]$PacketBridgeFakePeerInfo,
    [switch]$PacketBridgeBypassStartConnection,
    [switch]$PacketBridgeBypassStartConnectionClientOnly,
    [int]$PacketBridgeBypassStartConnectionStartFrame = 0,
    [switch]$PacketBridgeBypassWifiStart,
    [int]$PacketBridgeBypassWifiStartFrame = 0,
    [switch]$PacketBridgeForceTick,
    [int]$PacketBridgeForceTickStartFrame = 0,
    [int]$PacketBridgeForceTickBase = -1,
    [int]$HostPacketBridgeForceTickBase = -1,
    [int]$ClientPacketBridgeForceTickBase = -1,
    [switch]$PacketBridgeForceNetReady,
    [int]$PacketBridgeForceNetReadyStartFrame = 0,
    [int]$PacketBridgeForceNetReadyEndFrame = 0,
    [switch]$PacketBridgeForceNetReadyHostOnly,
    [switch]$PacketBridgeForceNetReadyClientOnly,
    [switch]$PacketBridgeForceNetReadyState10,
    [switch]$PacketBridgeForceNetReadyState10ClientOnly,
    [switch]$PacketBridgeForceCourseSelectReady,
    [int]$PacketBridgeForceCourseSelectReadyStartFrame = 0,
    [switch]$PacketBridgeForceStageSceneArg,
    [switch]$PacketBridgeForceLoadGameSM,
    [int]$PacketBridgeForceLoadGameSMStartFrame = 0,
    [int]$PacketBridgeForceLoadGameSMStep = 3,
    [int]$PacketBridgeForceLoadGameSMTimer = -1,
    [int]$PacketBridgeForceLoadGameSMFlags = -1,
    [switch]$PacketBridgeForceLoadGameSMRunUpdate,
    [switch]$PacketBridgeForceLoadGameSMRunUpdateAll,
    [switch]$PacketBridgeForceLoadGameSMPulseAction,
    [switch]$PacketBridgeForceLoadGameSMBaselineFlags,
    [switch]$PacketBridgeForceLoadGameSMPreload,
    [switch]$PacketBridgeForceLoadGameSMAllowCourseSelect,
    [switch]$PacketBridgeForceStagePacketWords,
    [switch]$PacketBridgeForceStagePacketWordsHostOnly,
    [switch]$PacketBridgeForceStagePacketWordsClientOnly,
    [int]$PacketBridgeForceStagePacketWordsStartFrame = 0,
    [int]$PacketBridgeForceStagePacketWordsEndFrame = 0,
    [switch]$PacketBridgeForceStageNet20OnStageScene,
    [switch]$PacketBridgeDummyAlloc,
    [int]$PacketBridgeDummyAllocFrame = 0,
    [int]$PacketBridgeDummyAllocSize = 0,
    [switch]$PacketBridgeScheduleLoadGameSM,
    [int]$PacketBridgeScheduleLoadGameSMFrame = 0,
    [string]$PacketBridgeSubMenuSchedule = "",
    [switch]$PacketBridgeSubMenuDirect,
    [switch]$PacketBridgeSubMenuCallCreate,
    [switch]$PacketBridgeForceStageStartSMFields,
    [int]$PacketBridgeForceStageStartSMFieldsStartFrame = 0,
    [switch]$PacketBridgeForceStageStartSMUseLoadStep,
    [switch]$PacketBridgeRunStageStartSMUpdate,
    [int]$PacketBridgeRunStageStartSMUpdateStartFrame = 0,
    [switch]$PacketBridgeRunVSConnectOnUpdate,
    [int]$PacketBridgeRunVSConnectOnUpdateStartFrame = 0,
    [switch]$PacketBridgeForceMvlFileCache,
    [int]$PacketBridgeForceMvlFileCacheStartFrame = 0,
    [switch]$PacketBridgeForceMvlLoadThread,
    [switch]$PacketBridgeForceMvlLoadThreadHostOnly,
    [switch]$PacketBridgeForceMvlLoadThreadClientOnly,
    [int]$PacketBridgeForceMvlLoadThreadStartFrame = 0,
    [int]$PacketBridgeLookupTickDelay = 0,
    [int]$PacketBridgeLocalInputDelay = -1,
    [switch]$PacketBridgeNeutralizeLocalInput,
    [switch]$PacketBridgePreserveLocalTouch,
    [int]$PacketBridgeSendDelayFrames = 0,
    [int]$PacketBridgeSendJitterFrames = 0,
    [int]$PacketBridgeMaxPumpEvents = 64,
    [switch]$PacketBridgeSuppressDisconnect,
    [switch]$PacketBridgeSuppressBlackout,
    [switch]$PacketBridgePreserveNetPointers,
    [switch]$PacketBridgeBypassNetReset,
    [switch]$PacketBridgeBypassNetDisconnect,
    [int]$PacketBridgeBypassNetDisconnectStartFrame = 0,
    [string]$PacketBridgeBypassNetDisconnectMode = "skip",
    [switch]$PacketBridgeForceTransferResult,
    [switch]$PacketBridgeForceTransferClientOnly,
    [int]$PacketBridgeForceTransferStartFrame = 0,
    [int]$PacketBridgeForceTransferResultValue = 8,
    [string]$NetRandomValue = "",
    [int]$NetRandomFrame = 0,
    [switch]$NetRandomAuto,
    [int]$PacketBridgeMaxTickLead = -1,
    [int]$PacketBridgeMaxFrameLead = -1,
    [int]$PacketBridgeThrottleTimeoutMs = 5000,
    [int]$PacketBridgeThrottleStartFrame = 0,
    [switch]$ForcePlayerCount,
    [switch]$ForcePlayerCountHostOnly,
    [switch]$ForcePlayerCountClientOnly,
    [int]$ForcePlayerCountStartFrame = 0,
    [int]$ForcePlayerCountEndFrame = 0,
    [int]$ForcePlayerCountValue = 2,
    [switch]$ForceStageSceneRuntimeWords,
    [switch]$ForceStageSceneRuntimeWordsHostOnly,
    [switch]$ForceStageSceneRuntimeWordsClientOnly,
    [int]$ForceStageSceneRuntimeWordsStartFrame = 0,
    [int]$ForceStageSceneRuntimeWordsEndFrame = 0,
    [string]$ForceStageSceneWord154 = "1",
    [string]$ForceStageSceneWord160 = "0xDA",
    [switch]$ForceStageCameraSlot,
    [switch]$ForceStageCameraSlotVerticalOnly,
    [int]$ForceStageCameraSlotStartFrame = 0,
    [int]$ForceStageCameraSlotEndFrame = 0,
    [int]$ForceStageCameraSlotSource = 0,
    [int]$ForceStageCameraSlotDest = 1,
    [switch]$ForceStageCameraObjectX,
    [int]$ForceStageCameraObjectXStartFrame = 0,
    [int]$ForceStageCameraObjectXEndFrame = 0,
    [string]$ForceStageCameraObjectXValue = "0",
    [string]$ForceStageCameraObjectZValue = "",
    [switch]$ForceStageCameraObjectXWriteDisplay,
    [switch]$ForceStageCameraObjectXWriteSlot,
    [int]$ForceStageCameraObjectXSlot = 1,
    [switch]$ForceStageFXSettings,
    [switch]$ForceStageFXSettingsHostOnly,
    [switch]$ForceStageFXSettingsClientOnly,
    [int]$ForceStageFXSettingsStartFrame = 0,
    [int]$ForceStageFXSettingsEndFrame = 0,
    [string]$ForceStageFXSettingsValue = "0x8000",
    [switch]$ForceStageSceneActive,
    [switch]$ForceStageSceneActiveHostOnly,
    [switch]$ForceStageSceneActiveClientOnly,
    [int]$ForceStageSceneActiveStartFrame = 0,
    [int]$ForceStageSceneActiveEndFrame = 0,
    [switch]$ForceStageActorFreezeFlag,
    [switch]$ForceStageActorFreezeFlagHostOnly,
    [switch]$ForceStageActorFreezeFlagClientOnly,
    [int]$ForceStageActorFreezeFlagStartFrame = 0,
    [int]$ForceStageActorFreezeFlagEndFrame = 0,
    [string]$ForceStageActorFreezeFlagValue = "0",
    [switch]$ForcePlayerDeathCounters,
    [switch]$ForcePlayerDeathCountersHostOnly,
    [switch]$ForcePlayerDeathCountersClientOnly,
    [int]$ForcePlayerDeathCountersStartFrame = 0,
    [int]$ForcePlayerDeathCountersEndFrame = 0,
    [int]$ForcePlayerDeathCounter0 = 0,
    [int]$ForcePlayerDeathCounter1 = 0,
    [switch]$ForcePlayerLives,
    [int]$ForcePlayerLife0 = 5,
    [int]$ForcePlayerLife1 = 5,
    [switch]$ForcePlayerInventoryPowerups,
    [int]$ForcePlayerInventoryPowerupsStartFrame = 0,
    [int]$ForcePlayerInventoryPowerupsEndFrame = 0,
    [int]$ForcePlayerInventoryPowerup0 = 0,
    [int]$ForcePlayerInventoryPowerup1 = 0,
    [switch]$ForcePlayerStarCounters,
    [int]$ForcePlayerStarCountersStartFrame = 0,
    [int]$ForcePlayerStarCountersEndFrame = 0,
    [int]$ForcePlayerBattleStars0 = 0,
    [int]$ForcePlayerBattleStars1 = 0,
    [int]$ForcePlayerDisplayedStars0 = 0,
    [int]$ForcePlayerDisplayedStars1 = 0,
    [int]$ForcePlayerCollectedStars0 = 0,
    [int]$ForcePlayerCollectedStars1 = 0,
    [switch]$RenderCameraAlias,
    [switch]$RenderCameraAliasAllRoles,
    [int]$RenderCameraAliasSourcePlayer = 1,
    [int]$RenderCameraAliasDestPlayer = 0,
    [int]$RenderCameraAliasStartFrame = 0,
    [int]$RenderCameraAliasEndFrame = 0,
    [switch]$ForceCameraFocusLoopCount,
    [switch]$ForceCameraFocusLoopCountHostOnly,
    [switch]$ForceCameraFocusLoopCountClientOnly,
    [int]$ForceCameraFocusLoopCountValue = 2,
    [int]$ForceCameraFocusLoopCountStartFrame = 0,
    [int]$ForceCameraFocusLoopCountEndFrame = 0,
    [switch]$TracePlayerLifeCalls,
    [switch]$TracePlayerLifeChanges,
    [switch]$TracePlayerDefeated,
    [switch]$TracePlayerRender,
    [int]$TracePlayerRenderStartFrame = 0,
    [int]$TracePlayerRenderEndFrame = 0,
    [switch]$TraceStageCamera,
    [int]$TraceStageCameraStartFrame = 0,
    [int]$TraceStageCameraEndFrame = 0,
    [int]$TraceStageCameraInterval = 1,
    [switch]$ForcePlayerSignalUnlock,
    [switch]$ForcePlayerSignalUnlockHostOnly,
    [switch]$ForcePlayerSignalUnlockClientOnly,
    [int]$ForcePlayerSignalUnlockStartFrame = 0,
    [int]$ForcePlayerSignalUnlockEndFrame = 0,
    [switch]$ForceStageSceneStartGate,
    [switch]$ForceStageSceneStartGateHostOnly,
    [switch]$ForceStageSceneStartGateClientOnly,
    [switch]$ForceStageSceneFadeReady,
    [switch]$ForceStageSceneInputLatch,
    [int]$ForceNetLocalAid = -1,
    [int]$ForceNetLocalAidStartFrame = 0,
    [int]$ForceNetLocalAidEndFrame = 0,
    [int]$ForceWifiCommunicatingCount = -1,
    [int]$ForceWifiCommunicatingStartFrame = 0,
    [int]$ForceWifiCommunicatingEndFrame = 0,
    [switch]$PacketBridgeArmOnly,
    [switch]$ScriptRemotePacket,
    [int]$ScriptRemotePacketPlayer = -1,
    [int]$ScriptRemotePacketInputInstance = -1,
    [string]$ScriptRemotePacketInputScript = "",
    [int]$ScriptRemotePacketStartFrame = 0,
    [int]$ScriptRemotePacketEndFrame = 0,
    [int]$ForceStageSceneStartGateStartFrame = 0,
    [int]$ForceStageSceneStartGateEndFrame = 0,
    [string]$ForceStageSceneStartGateValue = "1",
    [switch]$ForceStageSceneContinueGate,
    [switch]$ForceStageSceneContinueGateHostOnly,
    [switch]$ForceStageSceneContinueGateClientOnly,
    [int]$ForceStageSceneContinueGateStartFrame = 0,
    [int]$ForceStageSceneContinueGateEndFrame = 0,
    [string]$ForceStageSceneContinueGateValue = "1",
    [switch]$ForceStageSceneState3Gate,
    [switch]$ForceStageSceneState3GateHostOnly,
    [switch]$ForceStageSceneState3GateClientOnly,
    [int]$ForceStageSceneState3GateStartFrame = 0,
    [int]$ForceStageSceneState3GateEndFrame = 0,
    [string]$ForceStageSceneState3GateValue = "1",
    [switch]$ForceStageSceneEventFlags,
    [switch]$ForceStageSceneEventFlagsHostOnly,
    [switch]$ForceStageSceneEventFlagsClientOnly,
    [int]$ForceStageSceneEventFlagsStartFrame = 0,
    [int]$ForceStageSceneEventFlagsEndFrame = 0,
    [string]$ForceStageSceneEventFlagsValue = "0",
    [switch]$ForceMvlPlayerReady,
    [switch]$ForceMvlPlayerReadyHostOnly,
    [switch]$ForceMvlPlayerReadyClientOnly,
    [int]$ForceMvlPlayerReadyStartFrame = 0,
    [int]$ForceMvlPlayerReadyEndFrame = 0,
    [string]$ForceMvlPlayerReadyValue = "0xFF00",
    [switch]$ForceMvlPlayerReadySetA8EC,
    [string]$ForceMvlPlayerReadyA8ECValue = "0xFF",
    [switch]$ForceMvlRuntimeState,
    [switch]$ForceMvlRuntimeStateHostOnly,
    [switch]$ForceMvlRuntimeStateClientOnly,
    [int]$ForceMvlRuntimeStateStartFrame = 0,
    [int]$ForceMvlRuntimeStateEndFrame = 0,
    [string]$ForceMvlRuntimeStateValue = "3",
    [switch]$GuardPlayerModelRenderPtrs,
    [int]$GuardPlayerModelRenderPtrsStartFrame = 0,
    [int]$GuardPlayerModelRenderPtrsEndFrame = 0,
    [int]$DropMPAfterFrame = 0,
    [switch]$LanWanMode,
    [switch]$NoLanMP,
    [int]$LanMPRecvTimeoutMs = -1,
    [int]$LanMPMiscRecvTimeoutMs = -1,
    [int]$LanMPStaleMs = -1,
    [int]$LanMPReplySlackUs = -1,
    [int]$LanMPSendDelayMs = -1,
    [int]$HostLanMPSendDelayMs = -1,
    [int]$ClientLanMPSendDelayMs = -1,
    [switch]$LanMPReliable,
    [switch]$LanMPDropOldRegular,
    [switch]$LanMPAcceptAnyChannel,
    [switch]$DirectMvlBoot,
    [switch]$DirectMvlBootHostOnly,
    [switch]$DirectMvlBootClientOnly,
    [int]$DirectMvlBootFrame = 900,
    [int]$DirectMvlBootStage = 0,
    [switch]$DirectMvlBootLoadSM,
    [switch]$DirectMvlBootPatchLoadSMOnly,
    [switch]$DirectMvlBootCallUpdateSM,
    [switch]$DirectMvlBootCallStartLoad,
    [switch]$DirectMvlBootCallCourseSelect,
    [switch]$DirectMvlBootCallObjectCourseSelect,
    [switch]$SafeStartLoadCall,
    [int]$SafeStartLoadCallFrame = 1900,
    [string]$SafeStartLoadCallPC = "0x0200F944",
    [string]$HostSafeStartLoadCallPC = "",
    [string]$ClientSafeStartLoadCallPC = "",
    [switch]$SafeLoadLevelCall,
    [int]$SafeLoadLevelCallFrame = 1600,
    [string]$SafeLoadLevelCallPC = "0x0200F944",
    [switch]$SafeLoadLevelSessionReady,
    [switch]$SafeLoadLevelFilesReady,
    [string]$HostSafeLoadLevelPlayerID = "",
    [string]$ClientSafeLoadLevelPlayerID = "",
    [switch]$SafeCourseSelectCall,
    [int]$SafeCourseSelectCallFrame = 1900,
    [string]$SafeCourseSelectCallPC = "0x0200F944",
    [switch]$SafeCourseSelectFactoryCall,
    [int]$SafeCourseSelectFactoryCallFrame = 1900,
    [string]$SafeCourseSelectFactoryCallPC = "0x0200F944",
    [string]$HostSafeCourseSelectFactoryCallPC = "",
    [string]$ClientSafeCourseSelectFactoryCallPC = "",
    [switch]$SafeUpdateLoadGameCall,
    [int]$SafeUpdateLoadGameCallFrame = 1900,
    [string]$SafeUpdateLoadGameCallPC = "0x0200F944",
    [string]$HostSafeUpdateLoadGameCallPC = "",
    [string]$ClientSafeUpdateLoadGameCallPC = "",
    [switch]$SafeTryChangeSceneCall,
    [int]$SafeTryChangeSceneCallFrame = 1960,
    [string]$SafeTryChangeSceneCallPC = "0",
    [string]$HostSafeTryChangeSceneCallPC = "",
    [string]$ClientSafeTryChangeSceneCallPC = "",
    [string]$SafeTryChangeSceneTarget = "",
    [string]$SafeTryChangeScenePrevious = "",
    [switch]$SafeTryChangeSceneSetOnly,
    [switch]$SafeStageSceneFactoryCall,
    [int]$SafeStageSceneFactoryCallFrame = 2700,
    [string]$SafeStageSceneFactoryCallPC = "0",
    [switch]$SafeStageSceneFactoryCreateObject,
    [switch]$SafeStageSceneFactoryCreateObjectHostOnly,
    [switch]$SafeStageSceneFactoryCreateObjectClientOnly,
    [switch]$SafeStageSceneFactorySceneSwitch,
    [switch]$SafeStageSceneFactorySceneSwitchHostOnly,
    [switch]$SafeStageSceneFactorySceneSwitchClientOnly,
    [switch]$SafeStageSceneFactoryInactive,
    [string]$HostSafeStageSceneFactoryCallPC = "",
    [string]$ClientSafeStageSceneFactoryCallPC = "",
    [switch]$SafeMvlLoadThreadCall,
    [switch]$SafeMvlLoadThreadCallHostOnly,
    [switch]$SafeMvlLoadThreadCallClientOnly,
    [int]$SafeMvlLoadThreadCallFrame = 1833,
    [string]$SafeMvlLoadThreadCallPC = "0",
    [string]$SafeMvlLoadThreadCallMinSP = "",
    [string]$SafeMvlLoadThreadCallMode = "",
    [string]$SafeCallMinSP = "",
    [string]$SafeCallRequiredMode = "",
    [switch]$SafeCallProbe,
    [switch]$SafeCallProbeOnly,
    [string]$SafeCallProbeMinSP = "",
    [string]$SafeCallProbeMode = "",
    [int]$SafeCallProbeMax = 80,
    [switch]$PacketBridgeSafeCreateLoadGameSM,
    [int]$PacketBridgeSafeCreateLoadGameSMFrame = 0,
    [string]$PacketBridgeSafeCreateLoadGameSMPC = "0x02151E94",
    [switch]$ForceCourseSelectFactory,
    [switch]$ForceCourseSelectFactoryClientOnly,
    [int]$ForceCourseSelectFactoryFrame = 2300,
    [int]$HostForceCourseSelectFactoryPlayerArg = 1,
    [int]$ClientForceCourseSelectFactoryPlayerArg = 0,
    [switch]$CallTrace,
    [string]$CallTraceAddrs = "",
    [int]$CallTraceStartFrame = 0,
    [int]$CallTraceEndFrame = 0,
    [int]$CallTraceDumpLen = 32,
    [switch]$WriteTrace,
    [string]$WriteTraceAddrs = "",
    [int]$WriteTraceStartFrame = 0,
    [int]$WriteTraceEndFrame = 0,
    [switch]$BadJumpTrace,
    [switch]$AllowJit,
    [string]$HostMemPatchFile = "",
    [string]$ClientMemPatchFile = "",
    [string]$MemPatchFrame = "",
    [string]$MemPatchRanges = "",
    [ValidateSet("both", "host", "client")]
    [string]$RunRole = "both",
    [string]$Peer = "127.0.0.1",
    [string]$LanHost = "",
    [int]$HostStartupDelayMs = 1000,
    [int]$LanStartAttempts = 1,
    [switch]$SkipDisconnectScreenshotCheck,
    [switch]$SkipBlankScreenshotCheck,
    [switch]$SkipMvlStateCheck,
    [switch]$SkipGameplayActorCheck,
    [switch]$SkipArmAbortCheck,
    [switch]$RequireClientRemotePlayer0Movement,
    [string]$LogDir = "logs\nsmb-mvl-lan-route"
)

$ErrorActionPreference = "Stop"

if ($LanStartAttempts -gt 1) {
    for ($attempt = 1; $attempt -le $LanStartAttempts; $attempt++) {
        $attemptParams = @{} + $PSBoundParameters
        $attemptParams["LanStartAttempts"] = 1
        $attemptParams["LogDir"] = "$LogDir-attempt$attempt"
        try {
            & $PSCommandPath @attemptParams
            return
        } catch {
            $message = $_.Exception.Message
            $retryable =
                $message -like "*LAN start*" -or
                $message -like "*missing client LAN start*" -or
                $message -like "*missing host LAN start*" -or
                $message -like "*missing client frame limit*" -or
                $message -like "*missing host frame limit*" -or
                $message -like "*connection-dialog screenshot detected*" -or
                $message -like "*stageGroup=0x0 vsMode=0x0*"
            if (-not $retryable -or $attempt -ge $LanStartAttempts) {
                throw
            }
            Write-Host "retrying LAN route smoke after retryable startup failure: attempt $attempt/$LanStartAttempts"
            Start-Sleep -Milliseconds 500
        }
    }
}

$exePath = (Resolve-Path $Exe).Path
$romPath = (Resolve-Path $Rom).Path
$hostSourceRomPath = if ($HostRom) { (Resolve-Path $HostRom).Path } else { $romPath }
$clientSourceRomPath = if ($ClientRom) { (Resolve-Path $ClientRom).Path } else { $romPath }
$sourceInputPath = (Resolve-Path $InputScript).Path
New-Item -ItemType Directory -Force -Path $LogDir | Out-Null
$logRoot = (Resolve-Path $LogDir).Path

$hostRoot = Join-Path $logRoot "host-rom"
$clientRoot = Join-Path $logRoot "client-rom"
New-Item -ItemType Directory -Force -Path $hostRoot, $clientRoot | Out-Null
$hostRom = Join-Path $hostRoot "nsmb.nds"
$clientRom = Join-Path $clientRoot "nsmb.nds"
Copy-Item -Force $hostSourceRomPath $hostRom
Copy-Item -Force $clientSourceRomPath $clientRom

function Copy-SaveSiblings {
    param(
        [string]$SourceRom,
        [string]$TargetRoot
    )

    $base = [System.IO.Path]::Combine(
        [System.IO.Path]::GetDirectoryName($SourceRom),
        [System.IO.Path]::GetFileNameWithoutExtension($SourceRom))
    foreach ($suffix in @(".sav", ".sav.2")) {
        $source = "$base$suffix"
        if (Test-Path $source) {
            Copy-Item -Force $source (Join-Path $TargetRoot "nsmb$suffix")
        }
    }
}
Copy-SaveSiblings -SourceRom $hostSourceRomPath -TargetRoot $hostRoot
Copy-SaveSiblings -SourceRom $clientSourceRomPath -TargetRoot $clientRoot

$hostInput = Join-Path $logRoot "host.inputs"
$clientInput = Join-Path $logRoot "client.inputs"
Remove-Item -Force $hostInput, $clientInput -ErrorAction SilentlyContinue

function Convert-InputScriptForRole {
    param(
        [string]$Source,
        [string]$Destination,
        [string]$RoleInstance
    )

    $out = New-Object System.Collections.Generic.List[string]
    foreach ($line in Get-Content $Source) {
        $trimmed = $line.Trim()
        if (-not $trimmed -or $trimmed.StartsWith("#")) {
            $out.Add($line)
            continue
        }

        if ($trimmed -match "^(inst\d+)\s+(.+)$") {
            if ($matches[1] -eq $RoleInstance) {
                $out.Add($matches[2])
            }
            continue
        }

        $out.Add($line)
    }

    $utf8NoBom = New-Object System.Text.UTF8Encoding($false)
    [System.IO.File]::WriteAllLines($Destination, [string[]]$out, $utf8NoBom)
}

Convert-InputScriptForRole -Source $sourceInputPath -Destination $hostInput -RoleInstance "inst0"
Convert-InputScriptForRole -Source $sourceInputPath -Destination $clientInput -RoleInstance "inst1"

$hostOut = Join-Path $logRoot "host.stdout.txt"
$clientOut = Join-Path $logRoot "client.stdout.txt"
$hostHash = Join-Path $logRoot "host.hash.csv"
$clientHash = Join-Path $logRoot "client.hash.csv"
$hostGameStateTrace = Join-Path $logRoot "host.game-state.csv"
$clientGameStateTrace = Join-Path $logRoot "client.game-state.csv"
$hostLanMPTrace = Join-Path $logRoot "host.lanmp.csv"
$clientLanMPTrace = Join-Path $logRoot "client.lanmp.csv"
$hostPacketCapture = Join-Path $logRoot "host.packet-capture.csv"
$clientPacketCapture = Join-Path $logRoot "client.packet-capture.csv"
$hostRamDumps = Join-Path $logRoot "ram-host"
$clientRamDumps = Join-Path $logRoot "ram-client"
$hostScreens = Join-Path $logRoot "screens-host"
$clientScreens = Join-Path $logRoot "screens-client"
Remove-Item -Force $hostOut, $clientOut, $hostHash, $clientHash, "$hostOut.err", "$clientOut.err" -ErrorAction SilentlyContinue
Remove-Item -Force $hostGameStateTrace, $clientGameStateTrace, $hostLanMPTrace, $clientLanMPTrace, $hostPacketCapture, $clientPacketCapture -ErrorAction SilentlyContinue
Remove-Item -Recurse -Force $hostScreens, $clientScreens, $hostRamDumps, $clientRamDumps -ErrorAction SilentlyContinue

function Start-MelonLANProcess {
    param(
        [string]$Role,
        [string]$RoleRom,
        [string]$RoleInput,
        [string]$Stdout,
        [string]$HashLog,
        [string]$ScreenshotDir,
        [string]$GameStateTracePath,
        [string]$LanMPTracePath,
        [string]$PacketReplayFile,
        [string]$PacketCapturePath,
        [string]$RamDumpDir
    )

    $env:MELONDS_NSML_TEST = "1"
    $env:MELONDS_NSML_TEST_INSTANCES = "1"
    $roleFrames = $Frames
    if ($Role -eq "host" -and $HostFrames -gt 0) {
        $roleFrames = $HostFrames
    } elseif ($Role -eq "client" -and $ClientFrames -gt 0) {
        $roleFrames = $ClientFrames
    }
    $env:MELONDS_NSML_TEST_FRAMES = "$roleFrames"
    $env:MELONDS_NSML_ROLE = $Role
    $env:MELONDS_NSML_INPUT_SCRIPT = $RoleInput
    if ($NoFrameLimit) {
        $env:MELONDS_NSML_DISABLE_FRAME_LIMIT = "1"
    } else {
        Remove-Item Env:\MELONDS_NSML_DISABLE_FRAME_LIMIT -ErrorAction SilentlyContinue
    }
    if ($FixedFrameTime) {
        $env:MELONDS_NSML_FIXED_FRAME_TIMESTEP = "1"
    } else {
        Remove-Item Env:\MELONDS_NSML_FIXED_FRAME_TIMESTEP -ErrorAction SilentlyContinue
    }
    if ($TargetFps -gt 0.0) {
        $env:MELONDS_NSML_TARGET_FPS = $TargetFps.ToString([System.Globalization.CultureInfo]::InvariantCulture)
    } else {
        Remove-Item Env:\MELONDS_NSML_TARGET_FPS -ErrorAction SilentlyContinue
    }
    if ($NoHashLog) {
        $env:MELONDS_NSML_DISABLE_HASH = "1"
        Remove-Item Env:\MELONDS_NSML_HASH_LOG -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_HASH_INTERVAL -ErrorAction SilentlyContinue
    } else {
        Remove-Item Env:\MELONDS_NSML_DISABLE_HASH -ErrorAction SilentlyContinue
        $env:MELONDS_NSML_HASH_LOG = $HashLog
        $env:MELONDS_NSML_HASH_INTERVAL = "300"
    }
    $env:MELONDS_NSML_SCREENSHOT_DIR = $ScreenshotDir
    $env:MELONDS_NSML_SCREENSHOT_INTERVAL = "$ScreenshotInterval"
    if ($DirectMvlBoot) {
        $env:MELONDS_NSML_DIRECT_MVL_BOOT = "1"
        if ($DirectMvlBootHostOnly) { $env:MELONDS_NSML_DIRECT_MVL_BOOT_HOST_ONLY = "1" } else { Remove-Item Env:\MELONDS_NSML_DIRECT_MVL_BOOT_HOST_ONLY -ErrorAction SilentlyContinue }
        if ($DirectMvlBootClientOnly) { $env:MELONDS_NSML_DIRECT_MVL_BOOT_CLIENT_ONLY = "1" } else { Remove-Item Env:\MELONDS_NSML_DIRECT_MVL_BOOT_CLIENT_ONLY -ErrorAction SilentlyContinue }
        $env:MELONDS_NSML_DIRECT_MVL_BOOT_FRAME = "$DirectMvlBootFrame"
        $env:MELONDS_NSML_DIRECT_MVL_BOOT_STAGE = "$DirectMvlBootStage"
        $env:MELONDS_NSML_DIRECT_MVL_BOOT_PLAYER_ID = $(if ($Role -eq "client") { "1" } else { "0" })
        if ($DirectMvlBootLoadSM) { $env:MELONDS_NSML_DIRECT_MVL_BOOT_LOAD_SM = "1" } else { Remove-Item Env:\MELONDS_NSML_DIRECT_MVL_BOOT_LOAD_SM -ErrorAction SilentlyContinue }
        if ($DirectMvlBootPatchLoadSMOnly) { $env:MELONDS_NSML_DIRECT_MVL_BOOT_PATCH_LOAD_SM_ONLY = "1" } else { Remove-Item Env:\MELONDS_NSML_DIRECT_MVL_BOOT_PATCH_LOAD_SM_ONLY -ErrorAction SilentlyContinue }
        if ($DirectMvlBootCallUpdateSM) { $env:MELONDS_NSML_DIRECT_MVL_BOOT_CALL_UPDATE_SM = "1" } else { Remove-Item Env:\MELONDS_NSML_DIRECT_MVL_BOOT_CALL_UPDATE_SM -ErrorAction SilentlyContinue }
        if ($DirectMvlBootCallStartLoad) { $env:MELONDS_NSML_DIRECT_MVL_BOOT_CALL_START_LOAD = "1" } else { Remove-Item Env:\MELONDS_NSML_DIRECT_MVL_BOOT_CALL_START_LOAD -ErrorAction SilentlyContinue }
        if ($DirectMvlBootCallCourseSelect) { $env:MELONDS_NSML_DIRECT_MVL_BOOT_CALL_COURSE_SELECT = "1" } else { Remove-Item Env:\MELONDS_NSML_DIRECT_MVL_BOOT_CALL_COURSE_SELECT -ErrorAction SilentlyContinue }
        if ($DirectMvlBootCallObjectCourseSelect) { $env:MELONDS_NSML_DIRECT_MVL_BOOT_CALL_OBJECT_COURSE_SELECT = "1" } else { Remove-Item Env:\MELONDS_NSML_DIRECT_MVL_BOOT_CALL_OBJECT_COURSE_SELECT -ErrorAction SilentlyContinue }
    } else {
        Remove-Item Env:\MELONDS_NSML_DIRECT_MVL_BOOT -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_DIRECT_MVL_BOOT_HOST_ONLY -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_DIRECT_MVL_BOOT_CLIENT_ONLY -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_DIRECT_MVL_BOOT_FRAME -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_DIRECT_MVL_BOOT_STAGE -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_DIRECT_MVL_BOOT_PLAYER_ID -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_DIRECT_MVL_BOOT_LOAD_SM -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_DIRECT_MVL_BOOT_PATCH_LOAD_SM_ONLY -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_DIRECT_MVL_BOOT_CALL_UPDATE_SM -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_DIRECT_MVL_BOOT_CALL_START_LOAD -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_DIRECT_MVL_BOOT_CALL_COURSE_SELECT -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_DIRECT_MVL_BOOT_CALL_OBJECT_COURSE_SELECT -ErrorAction SilentlyContinue
    }
    if ($SafeStartLoadCall) {
        $env:MELONDS_NSML_SAFE_START_LOAD_CALL = "1"
        $env:MELONDS_NSML_SAFE_START_LOAD_CALL_FRAME = "$SafeStartLoadCallFrame"
        $roleSafeStartLoadCallPC = $SafeStartLoadCallPC
        if ($Role -eq "host" -and $HostSafeStartLoadCallPC) {
            $roleSafeStartLoadCallPC = $HostSafeStartLoadCallPC
        } elseif ($Role -eq "client" -and $ClientSafeStartLoadCallPC) {
            $roleSafeStartLoadCallPC = $ClientSafeStartLoadCallPC
        }
        $env:MELONDS_NSML_SAFE_START_LOAD_CALL_PC = "$roleSafeStartLoadCallPC"
    } else {
        Remove-Item Env:\MELONDS_NSML_SAFE_START_LOAD_CALL -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_SAFE_START_LOAD_CALL_FRAME -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_SAFE_START_LOAD_CALL_PC -ErrorAction SilentlyContinue
    }
    if ($SafeLoadLevelCall) {
        $env:MELONDS_NSML_SAFE_LOAD_LEVEL_CALL = "1"
        $env:MELONDS_NSML_SAFE_LOAD_LEVEL_CALL_FRAME = "$SafeLoadLevelCallFrame"
        $env:MELONDS_NSML_SAFE_LOAD_LEVEL_CALL_PC = "$SafeLoadLevelCallPC"
        if ($SafeLoadLevelSessionReady) {
            $env:MELONDS_NSML_SAFE_LOAD_LEVEL_SESSION_READY = "1"
        } else {
            Remove-Item Env:\MELONDS_NSML_SAFE_LOAD_LEVEL_SESSION_READY -ErrorAction SilentlyContinue
        }
        if ($SafeLoadLevelFilesReady) {
            $env:MELONDS_NSML_SAFE_LOAD_LEVEL_FILES_READY = "1"
        } else {
            Remove-Item Env:\MELONDS_NSML_SAFE_LOAD_LEVEL_FILES_READY -ErrorAction SilentlyContinue
        }
        if ($Role -eq "client" -and $ClientSafeLoadLevelPlayerID) {
            $env:MELONDS_NSML_SAFE_LOAD_LEVEL_PLAYER_ID = $ClientSafeLoadLevelPlayerID
        } elseif ($Role -eq "host" -and $HostSafeLoadLevelPlayerID) {
            $env:MELONDS_NSML_SAFE_LOAD_LEVEL_PLAYER_ID = $HostSafeLoadLevelPlayerID
        } else {
            $env:MELONDS_NSML_SAFE_LOAD_LEVEL_PLAYER_ID = $(if ($Role -eq "client") { "1" } else { "0" })
        }
    } else {
        Remove-Item Env:\MELONDS_NSML_SAFE_LOAD_LEVEL_CALL -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_SAFE_LOAD_LEVEL_CALL_FRAME -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_SAFE_LOAD_LEVEL_CALL_PC -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_SAFE_LOAD_LEVEL_SESSION_READY -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_SAFE_LOAD_LEVEL_FILES_READY -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_SAFE_LOAD_LEVEL_PLAYER_ID -ErrorAction SilentlyContinue
    }
    if ($SafeCourseSelectCall) {
        $env:MELONDS_NSML_SAFE_COURSE_SELECT_CALL = "1"
        $env:MELONDS_NSML_SAFE_COURSE_SELECT_CALL_FRAME = "$SafeCourseSelectCallFrame"
        $env:MELONDS_NSML_SAFE_COURSE_SELECT_CALL_PC = "$SafeCourseSelectCallPC"
    } else {
        Remove-Item Env:\MELONDS_NSML_SAFE_COURSE_SELECT_CALL -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_SAFE_COURSE_SELECT_CALL_FRAME -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_SAFE_COURSE_SELECT_CALL_PC -ErrorAction SilentlyContinue
    }
    if ($SafeCourseSelectFactoryCall) {
        $env:MELONDS_NSML_SAFE_COURSE_SELECT_FACTORY_CALL = "1"
        $env:MELONDS_NSML_SAFE_COURSE_SELECT_FACTORY_CALL_FRAME = "$SafeCourseSelectFactoryCallFrame"
        $roleSafeCourseSelectFactoryCallPC = $SafeCourseSelectFactoryCallPC
        if ($Role -eq "host" -and $HostSafeCourseSelectFactoryCallPC) {
            $roleSafeCourseSelectFactoryCallPC = $HostSafeCourseSelectFactoryCallPC
        } elseif ($Role -eq "client" -and $ClientSafeCourseSelectFactoryCallPC) {
            $roleSafeCourseSelectFactoryCallPC = $ClientSafeCourseSelectFactoryCallPC
        }
        $env:MELONDS_NSML_SAFE_COURSE_SELECT_FACTORY_CALL_PC = "$roleSafeCourseSelectFactoryCallPC"
    } else {
        Remove-Item Env:\MELONDS_NSML_SAFE_COURSE_SELECT_FACTORY_CALL -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_SAFE_COURSE_SELECT_FACTORY_CALL_FRAME -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_SAFE_COURSE_SELECT_FACTORY_CALL_PC -ErrorAction SilentlyContinue
    }
    if ($SafeUpdateLoadGameCall) {
        $env:MELONDS_NSML_SAFE_UPDATE_LOAD_GAME_CALL = "1"
        $env:MELONDS_NSML_SAFE_UPDATE_LOAD_GAME_CALL_FRAME = "$SafeUpdateLoadGameCallFrame"
        $roleSafeUpdateLoadGameCallPC = $SafeUpdateLoadGameCallPC
        if ($Role -eq "host" -and $HostSafeUpdateLoadGameCallPC) {
            $roleSafeUpdateLoadGameCallPC = $HostSafeUpdateLoadGameCallPC
        } elseif ($Role -eq "client" -and $ClientSafeUpdateLoadGameCallPC) {
            $roleSafeUpdateLoadGameCallPC = $ClientSafeUpdateLoadGameCallPC
        }
        $env:MELONDS_NSML_SAFE_UPDATE_LOAD_GAME_CALL_PC = "$roleSafeUpdateLoadGameCallPC"
    } else {
        Remove-Item Env:\MELONDS_NSML_SAFE_UPDATE_LOAD_GAME_CALL -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_SAFE_UPDATE_LOAD_GAME_CALL_FRAME -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_SAFE_UPDATE_LOAD_GAME_CALL_PC -ErrorAction SilentlyContinue
    }
    if ($SafeTryChangeSceneCall) {
        $env:MELONDS_NSML_SAFE_TRY_CHANGE_SCENE_CALL = "1"
        $env:MELONDS_NSML_SAFE_TRY_CHANGE_SCENE_CALL_FRAME = "$SafeTryChangeSceneCallFrame"
        $roleSafeTryChangeSceneCallPC = $SafeTryChangeSceneCallPC
        if ($Role -eq "host" -and $HostSafeTryChangeSceneCallPC) {
            $roleSafeTryChangeSceneCallPC = $HostSafeTryChangeSceneCallPC
        } elseif ($Role -eq "client" -and $ClientSafeTryChangeSceneCallPC) {
            $roleSafeTryChangeSceneCallPC = $ClientSafeTryChangeSceneCallPC
        }
        $env:MELONDS_NSML_SAFE_TRY_CHANGE_SCENE_CALL_PC = "$roleSafeTryChangeSceneCallPC"
        if ($SafeTryChangeSceneTarget) { $env:MELONDS_NSML_SAFE_TRY_CHANGE_SCENE_TARGET = "$SafeTryChangeSceneTarget" } else { Remove-Item Env:\MELONDS_NSML_SAFE_TRY_CHANGE_SCENE_TARGET -ErrorAction SilentlyContinue }
        if ($SafeTryChangeScenePrevious) { $env:MELONDS_NSML_SAFE_TRY_CHANGE_SCENE_PREVIOUS = "$SafeTryChangeScenePrevious" } else { Remove-Item Env:\MELONDS_NSML_SAFE_TRY_CHANGE_SCENE_PREVIOUS -ErrorAction SilentlyContinue }
        if ($SafeTryChangeSceneSetOnly) { $env:MELONDS_NSML_SAFE_TRY_CHANGE_SCENE_SET_ONLY = "1" } else { Remove-Item Env:\MELONDS_NSML_SAFE_TRY_CHANGE_SCENE_SET_ONLY -ErrorAction SilentlyContinue }
    } else {
        Remove-Item Env:\MELONDS_NSML_SAFE_TRY_CHANGE_SCENE_CALL -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_SAFE_TRY_CHANGE_SCENE_CALL_FRAME -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_SAFE_TRY_CHANGE_SCENE_CALL_PC -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_SAFE_TRY_CHANGE_SCENE_TARGET -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_SAFE_TRY_CHANGE_SCENE_PREVIOUS -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_SAFE_TRY_CHANGE_SCENE_SET_ONLY -ErrorAction SilentlyContinue
    }
    if ($SafeStageSceneFactoryCall) {
        $env:MELONDS_NSML_SAFE_STAGE_SCENE_FACTORY_CALL = "1"
        $env:MELONDS_NSML_SAFE_STAGE_SCENE_FACTORY_CALL_FRAME = "$SafeStageSceneFactoryCallFrame"
        $roleSafeStageSceneFactoryCallPC = $SafeStageSceneFactoryCallPC
        if ($Role -eq "host" -and $HostSafeStageSceneFactoryCallPC) {
            $roleSafeStageSceneFactoryCallPC = $HostSafeStageSceneFactoryCallPC
        } elseif ($Role -eq "client" -and $ClientSafeStageSceneFactoryCallPC) {
            $roleSafeStageSceneFactoryCallPC = $ClientSafeStageSceneFactoryCallPC
        }
        $env:MELONDS_NSML_SAFE_STAGE_SCENE_FACTORY_CALL_PC = "$roleSafeStageSceneFactoryCallPC"
        $safeStageFactoryCreateObjectForRole = $SafeStageSceneFactoryCreateObject
        if ($SafeStageSceneFactoryCreateObjectHostOnly -and $Role -ne "host") {
            $safeStageFactoryCreateObjectForRole = $false
        }
        if ($SafeStageSceneFactoryCreateObjectClientOnly -and $Role -ne "client") {
            $safeStageFactoryCreateObjectForRole = $false
        }
        if ($safeStageFactoryCreateObjectForRole) {
            $env:MELONDS_NSML_SAFE_STAGE_SCENE_FACTORY_CREATE_OBJECT = "1"
        } else {
            Remove-Item Env:\MELONDS_NSML_SAFE_STAGE_SCENE_FACTORY_CREATE_OBJECT -ErrorAction SilentlyContinue
        }
        $safeStageFactorySceneSwitchForRole = $SafeStageSceneFactorySceneSwitch
        if ($SafeStageSceneFactorySceneSwitchHostOnly -and $Role -ne "host") {
            $safeStageFactorySceneSwitchForRole = $false
        }
        if ($SafeStageSceneFactorySceneSwitchClientOnly -and $Role -ne "client") {
            $safeStageFactorySceneSwitchForRole = $false
        }
        if ($safeStageFactorySceneSwitchForRole) {
            $env:MELONDS_NSML_SAFE_STAGE_SCENE_FACTORY_SCENE_SWITCH = "1"
        } else {
            Remove-Item Env:\MELONDS_NSML_SAFE_STAGE_SCENE_FACTORY_SCENE_SWITCH -ErrorAction SilentlyContinue
        }
        if ($SafeStageSceneFactoryInactive) {
            $env:MELONDS_NSML_SAFE_STAGE_SCENE_FACTORY_INACTIVE = "1"
        } else {
            Remove-Item Env:\MELONDS_NSML_SAFE_STAGE_SCENE_FACTORY_INACTIVE -ErrorAction SilentlyContinue
        }
    } else {
        Remove-Item Env:\MELONDS_NSML_SAFE_STAGE_SCENE_FACTORY_CALL -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_SAFE_STAGE_SCENE_FACTORY_CALL_FRAME -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_SAFE_STAGE_SCENE_FACTORY_CALL_PC -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_SAFE_STAGE_SCENE_FACTORY_CREATE_OBJECT -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_SAFE_STAGE_SCENE_FACTORY_SCENE_SWITCH -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_SAFE_STAGE_SCENE_FACTORY_INACTIVE -ErrorAction SilentlyContinue
    }
    if ($SafeCallMinSP) {
        $env:MELONDS_NSML_SAFE_CALL_MIN_SP = "$SafeCallMinSP"
    } else {
        Remove-Item Env:\MELONDS_NSML_SAFE_CALL_MIN_SP -ErrorAction SilentlyContinue
    }
    if ($SafeCallRequiredMode) {
        $env:MELONDS_NSML_SAFE_CALL_REQUIRED_MODE = "$SafeCallRequiredMode"
    } else {
        Remove-Item Env:\MELONDS_NSML_SAFE_CALL_REQUIRED_MODE -ErrorAction SilentlyContinue
    }
    $safeMvlLoadThreadForRole = $SafeMvlLoadThreadCall
    if ($SafeMvlLoadThreadCallHostOnly -and $Role -ne "host") {
        $safeMvlLoadThreadForRole = $false
    }
    if ($SafeMvlLoadThreadCallClientOnly -and $Role -ne "client") {
        $safeMvlLoadThreadForRole = $false
    }
    if ($safeMvlLoadThreadForRole) {
        $env:MELONDS_NSML_SAFE_MVL_LOAD_THREAD_CALL = "1"
        $env:MELONDS_NSML_SAFE_MVL_LOAD_THREAD_CALL_FRAME = "$SafeMvlLoadThreadCallFrame"
        $env:MELONDS_NSML_SAFE_MVL_LOAD_THREAD_CALL_PC = "$SafeMvlLoadThreadCallPC"
        if ($SafeMvlLoadThreadCallMinSP) {
            $env:MELONDS_NSML_SAFE_MVL_LOAD_THREAD_CALL_MIN_SP = "$SafeMvlLoadThreadCallMinSP"
        } else {
            Remove-Item Env:\MELONDS_NSML_SAFE_MVL_LOAD_THREAD_CALL_MIN_SP -ErrorAction SilentlyContinue
        }
        if ($SafeMvlLoadThreadCallMode) {
            $env:MELONDS_NSML_SAFE_MVL_LOAD_THREAD_CALL_MODE = "$SafeMvlLoadThreadCallMode"
        } else {
            Remove-Item Env:\MELONDS_NSML_SAFE_MVL_LOAD_THREAD_CALL_MODE -ErrorAction SilentlyContinue
        }
    } else {
        Remove-Item Env:\MELONDS_NSML_SAFE_MVL_LOAD_THREAD_CALL -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_SAFE_MVL_LOAD_THREAD_CALL_FRAME -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_SAFE_MVL_LOAD_THREAD_CALL_PC -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_SAFE_MVL_LOAD_THREAD_CALL_MIN_SP -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_SAFE_MVL_LOAD_THREAD_CALL_MODE -ErrorAction SilentlyContinue
    }
    if ($SafeCallProbe) {
        $env:MELONDS_NSML_SAFE_CALL_PROBE = "1"
        $env:MELONDS_NSML_SAFE_CALL_PROBE_MAX = "$SafeCallProbeMax"
        if ($SafeCallProbeOnly) { $env:MELONDS_NSML_SAFE_CALL_PROBE_ONLY = "1" } else { Remove-Item Env:\MELONDS_NSML_SAFE_CALL_PROBE_ONLY -ErrorAction SilentlyContinue }
        if ($SafeCallProbeMinSP) { $env:MELONDS_NSML_SAFE_CALL_PROBE_MIN_SP = "$SafeCallProbeMinSP" } else { Remove-Item Env:\MELONDS_NSML_SAFE_CALL_PROBE_MIN_SP -ErrorAction SilentlyContinue }
        if ($SafeCallProbeMode) { $env:MELONDS_NSML_SAFE_CALL_PROBE_MODE = "$SafeCallProbeMode" } else { Remove-Item Env:\MELONDS_NSML_SAFE_CALL_PROBE_MODE -ErrorAction SilentlyContinue }
    } else {
        Remove-Item Env:\MELONDS_NSML_SAFE_CALL_PROBE -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_SAFE_CALL_PROBE_ONLY -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_SAFE_CALL_PROBE_MIN_SP -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_SAFE_CALL_PROBE_MODE -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_SAFE_CALL_PROBE_MAX -ErrorAction SilentlyContinue
    }
    if ($PacketBridgeSafeCreateLoadGameSM) {
        $safeCreateLoadFrame = $PacketBridgeSafeCreateLoadGameSMFrame
        if ($safeCreateLoadFrame -le 0) {
            $safeCreateLoadFrame = $PacketBridgeForceLoadGameSMStartFrame
        }
        if ($safeCreateLoadFrame -le 0) {
            $safeCreateLoadFrame = 1840
        }
        $env:MELONDS_NSML_SAFE_CREATE_LOAD_GAME_CALL = "1"
        $env:MELONDS_NSML_SAFE_CREATE_LOAD_GAME_CALL_FRAME = "$safeCreateLoadFrame"
        $env:MELONDS_NSML_SAFE_CREATE_LOAD_GAME_CALL_PC = "$PacketBridgeSafeCreateLoadGameSMPC"
    } else {
        Remove-Item Env:\MELONDS_NSML_SAFE_CREATE_LOAD_GAME_CALL -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_SAFE_CREATE_LOAD_GAME_CALL_FRAME -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_SAFE_CREATE_LOAD_GAME_CALL_PC -ErrorAction SilentlyContinue
    }
    $packetBridgeScheduleLoadGameSMFromEnv = [bool]$env:MELONDS_NSML_FORCE_SCHEDULE_LOAD_GAME_SM_ARGS
    if ($PacketBridgeScheduleLoadGameSM -or $packetBridgeScheduleLoadGameSMFromEnv) {
        $safeScheduleFrame = $PacketBridgeScheduleLoadGameSMFrame
        if ($safeScheduleFrame -le 0) {
            $safeScheduleFrame = $PacketBridgeForceLoadGameSMStartFrame
        }
        if ($safeScheduleFrame -le 0 -and $env:MELONDS_NSML_FORCE_SCHEDULE_LOAD_GAME_SM_ARGS_FRAME) {
            $safeScheduleFrame = [int]$env:MELONDS_NSML_FORCE_SCHEDULE_LOAD_GAME_SM_ARGS_FRAME
        }
        if ($safeScheduleFrame -le 0) {
            $safeScheduleFrame = 0
        }
        $env:MELONDS_NSML_FORCE_SCHEDULE_LOAD_GAME_SM_ARGS = "1"
        $env:MELONDS_NSML_FORCE_SCHEDULE_LOAD_GAME_SM_ARGS_FRAME = "$safeScheduleFrame"
    } else {
        Remove-Item Env:\MELONDS_NSML_FORCE_SCHEDULE_LOAD_GAME_SM_ARGS -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_FORCE_SCHEDULE_LOAD_GAME_SM_ARGS_FRAME -ErrorAction SilentlyContinue
    }
    if ($PacketBridgeSubMenuSchedule) {
        $env:MELONDS_NSML_PACKET_BRIDGE_SUBMENU_SCHEDULE = "$PacketBridgeSubMenuSchedule"
    } else {
        Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_SUBMENU_SCHEDULE -ErrorAction SilentlyContinue
    }
    if ($PacketBridgeSubMenuDirect) {
        $env:MELONDS_NSML_PACKET_BRIDGE_SUBMENU_DIRECT = "1"
    } else {
        Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_SUBMENU_DIRECT -ErrorAction SilentlyContinue
    }
    if ($PacketBridgeSubMenuCallCreate) {
        $env:MELONDS_NSML_PACKET_BRIDGE_SUBMENU_CALL_CREATE = "1"
    } else {
        Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_SUBMENU_CALL_CREATE -ErrorAction SilentlyContinue
    }
    if ($PacketBridgeForceStageStartSMFields) {
        $env:MELONDS_NSML_PACKET_BRIDGE_FORCE_STAGE_START_SM_FIELDS = "1"
        $env:MELONDS_NSML_PACKET_BRIDGE_FORCE_STAGE_START_SM_FIELDS_START_FRAME = "$PacketBridgeForceStageStartSMFieldsStartFrame"
        if ($PacketBridgeForceStageStartSMUseLoadStep) { $env:MELONDS_NSML_PACKET_BRIDGE_FORCE_STAGE_START_SM_USE_LOAD_STEP = "1" } else { Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_FORCE_STAGE_START_SM_USE_LOAD_STEP -ErrorAction SilentlyContinue }
    } else {
        Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_FORCE_STAGE_START_SM_FIELDS -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_FORCE_STAGE_START_SM_FIELDS_START_FRAME -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_FORCE_STAGE_START_SM_USE_LOAD_STEP -ErrorAction SilentlyContinue
    }
    if ($PacketBridgeRunStageStartSMUpdate) {
        $env:MELONDS_NSML_PACKET_BRIDGE_RUN_STAGE_START_SM_UPDATE = "1"
        $env:MELONDS_NSML_PACKET_BRIDGE_RUN_STAGE_START_SM_UPDATE_START_FRAME = "$PacketBridgeRunStageStartSMUpdateStartFrame"
    } else {
        Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_RUN_STAGE_START_SM_UPDATE -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_RUN_STAGE_START_SM_UPDATE_START_FRAME -ErrorAction SilentlyContinue
    }
    if ($PacketBridgeRunVSConnectOnUpdate) {
        $env:MELONDS_NSML_PACKET_BRIDGE_RUN_VSCONNECT_ON_UPDATE = "1"
        $env:MELONDS_NSML_PACKET_BRIDGE_RUN_VSCONNECT_ON_UPDATE_START_FRAME = "$PacketBridgeRunVSConnectOnUpdateStartFrame"
    } else {
        Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_RUN_VSCONNECT_ON_UPDATE -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_RUN_VSCONNECT_ON_UPDATE_START_FRAME -ErrorAction SilentlyContinue
    }
    if ($ForceCourseSelectFactory -and (-not $ForceCourseSelectFactoryClientOnly -or $Role -eq "client")) {
        $env:MELONDS_NSML_FORCE_COURSE_SELECT_FACTORY = "1"
        $env:MELONDS_NSML_FORCE_COURSE_SELECT_FACTORY_FRAME = "$ForceCourseSelectFactoryFrame"
        if ($Role -eq "host") {
            $env:MELONDS_NSML_FORCE_COURSE_SELECT_FACTORY_PLAYER_ARG = "$HostForceCourseSelectFactoryPlayerArg"
        } else {
            $env:MELONDS_NSML_FORCE_COURSE_SELECT_FACTORY_PLAYER_ARG = "$ClientForceCourseSelectFactoryPlayerArg"
        }
    } else {
        Remove-Item Env:\MELONDS_NSML_FORCE_COURSE_SELECT_FACTORY -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_FORCE_COURSE_SELECT_FACTORY_FRAME -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_FORCE_COURSE_SELECT_FACTORY_PLAYER_ARG -ErrorAction SilentlyContinue
    }
    if ($GuardPlayerModelRenderPtrs) {
        $env:MELONDS_NSML_GUARD_PLAYER_MODEL_RENDER_PTRS = "1"
        if ($GuardPlayerModelRenderPtrsStartFrame -gt 0) { $env:MELONDS_NSML_GUARD_PLAYER_MODEL_RENDER_PTRS_START_FRAME = "$GuardPlayerModelRenderPtrsStartFrame" } else { Remove-Item Env:\MELONDS_NSML_GUARD_PLAYER_MODEL_RENDER_PTRS_START_FRAME -ErrorAction SilentlyContinue }
        if ($GuardPlayerModelRenderPtrsEndFrame -gt 0) { $env:MELONDS_NSML_GUARD_PLAYER_MODEL_RENDER_PTRS_END_FRAME = "$GuardPlayerModelRenderPtrsEndFrame" } else { Remove-Item Env:\MELONDS_NSML_GUARD_PLAYER_MODEL_RENDER_PTRS_END_FRAME -ErrorAction SilentlyContinue }
    } else {
        Remove-Item Env:\MELONDS_NSML_GUARD_PLAYER_MODEL_RENDER_PTRS -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_GUARD_PLAYER_MODEL_RENDER_PTRS_START_FRAME -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_GUARD_PLAYER_MODEL_RENDER_PTRS_END_FRAME -ErrorAction SilentlyContinue
    }
    if ($CallTrace) {
        $env:MELONDS_NSML_CALL_TRACE = "1"
        $env:MELONDS_NSML_CALL_TRACE_LOG = "$Stdout.call-trace.csv"
        if ($CallTraceAddrs) { $env:MELONDS_NSML_CALL_TRACE_ADDRS = $CallTraceAddrs } else { Remove-Item Env:\MELONDS_NSML_CALL_TRACE_ADDRS -ErrorAction SilentlyContinue }
        if ($CallTraceStartFrame -gt 0) { $env:MELONDS_NSML_CALL_TRACE_START_FRAME = "$CallTraceStartFrame" } else { Remove-Item Env:\MELONDS_NSML_CALL_TRACE_START_FRAME -ErrorAction SilentlyContinue }
        if ($CallTraceEndFrame -gt 0) { $env:MELONDS_NSML_CALL_TRACE_END_FRAME = "$CallTraceEndFrame" } else { Remove-Item Env:\MELONDS_NSML_CALL_TRACE_END_FRAME -ErrorAction SilentlyContinue }
        $env:MELONDS_NSML_CALL_TRACE_DUMP_LEN = "$CallTraceDumpLen"
    } else {
        Remove-Item Env:\MELONDS_NSML_CALL_TRACE -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_CALL_TRACE_LOG -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_CALL_TRACE_ADDRS -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_CALL_TRACE_START_FRAME -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_CALL_TRACE_END_FRAME -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_CALL_TRACE_DUMP_LEN -ErrorAction SilentlyContinue
    }
    if ($WriteTrace) {
        $env:MELONDS_NSML_WRITE_TRACE = "1"
        $env:MELONDS_NSML_WRITE_TRACE_LOG = "$Stdout.write-trace.csv"
        if ($WriteTraceAddrs) { $env:MELONDS_NSML_WRITE_TRACE_ADDRS = $WriteTraceAddrs } else { Remove-Item Env:\MELONDS_NSML_WRITE_TRACE_ADDRS -ErrorAction SilentlyContinue }
        if ($WriteTraceStartFrame -gt 0) { $env:MELONDS_NSML_WRITE_TRACE_START_FRAME = "$WriteTraceStartFrame" } else { Remove-Item Env:\MELONDS_NSML_WRITE_TRACE_START_FRAME -ErrorAction SilentlyContinue }
        if ($WriteTraceEndFrame -gt 0) { $env:MELONDS_NSML_WRITE_TRACE_END_FRAME = "$WriteTraceEndFrame" } else { Remove-Item Env:\MELONDS_NSML_WRITE_TRACE_END_FRAME -ErrorAction SilentlyContinue }
    } else {
        Remove-Item Env:\MELONDS_NSML_WRITE_TRACE -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_WRITE_TRACE_LOG -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_WRITE_TRACE_ADDRS -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_WRITE_TRACE_START_FRAME -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_WRITE_TRACE_END_FRAME -ErrorAction SilentlyContinue
    }
    if ($BadJumpTrace) {
        $env:MELONDS_NSML_BAD_JUMP_TRACE = "1"
    } else {
        Remove-Item Env:\MELONDS_NSML_BAD_JUMP_TRACE -ErrorAction SilentlyContinue
    }
    if ($GameStateTrace) {
        $env:MELONDS_NSML_GAME_STATE_TRACE = $GameStateTracePath
        $env:MELONDS_NSML_GAME_STATE_TRACE_INTERVAL = "$GameStateTraceInterval"
        if ($GameStateTraceStartFrame -gt 0) { $env:MELONDS_NSML_GAME_STATE_TRACE_START_FRAME = "$GameStateTraceStartFrame" } else { Remove-Item Env:\MELONDS_NSML_GAME_STATE_TRACE_START_FRAME -ErrorAction SilentlyContinue }
        if ($GameStateTraceEndFrame -gt 0) { $env:MELONDS_NSML_GAME_STATE_TRACE_END_FRAME = "$GameStateTraceEndFrame" } else { Remove-Item Env:\MELONDS_NSML_GAME_STATE_TRACE_END_FRAME -ErrorAction SilentlyContinue }
        if ($GameStateTraceExtended) {
            $env:MELONDS_NSML_GAME_STATE_TRACE_EXTENDED = "1"
        } else {
            Remove-Item Env:\MELONDS_NSML_GAME_STATE_TRACE_EXTENDED -ErrorAction SilentlyContinue
        }
    } else {
        Remove-Item Env:\MELONDS_NSML_GAME_STATE_TRACE -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_GAME_STATE_TRACE_INTERVAL -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_GAME_STATE_TRACE_START_FRAME -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_GAME_STATE_TRACE_END_FRAME -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_GAME_STATE_TRACE_EXTENDED -ErrorAction SilentlyContinue
    }
    if ($StateSync) {
        $env:MELONDS_NSML_STATE_SYNC = "1"
        $env:MELONDS_NSML_STATE_SYNC_INTERVAL = "$StateSyncInterval"
        if ($StateApply) {
            $env:MELONDS_NSML_STATE_APPLY = "1"
        } else {
            Remove-Item Env:\MELONDS_NSML_STATE_APPLY -ErrorAction SilentlyContinue
        }
        if ($StateSyncExtended) {
            $env:MELONDS_NSML_STATE_SYNC_EXTENDED = "1"
        } else {
            Remove-Item Env:\MELONDS_NSML_STATE_SYNC_EXTENDED -ErrorAction SilentlyContinue
        }
        if ($StateApplyMode) {
            $env:MELONDS_NSML_STATE_APPLY_MODE = $StateApplyMode
        } else {
            Remove-Item Env:\MELONDS_NSML_STATE_APPLY_MODE -ErrorAction SilentlyContinue
        }
    } else {
        Remove-Item Env:\MELONDS_NSML_STATE_SYNC -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_STATE_APPLY -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_STATE_SYNC_INTERVAL -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_STATE_SYNC_EXTENDED -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_STATE_APPLY_MODE -ErrorAction SilentlyContinue
    }
    if ($RamDumpFrames -or $RamDumpInterval -gt 0) {
        $env:MELONDS_NSML_RAM_DUMP_DIR = $RamDumpDir
        $env:MELONDS_NSML_RAM_DUMP_FRAMES = $RamDumpFrames
        $env:MELONDS_NSML_RAM_DUMP_INTERVAL = "$RamDumpInterval"
    } else {
        Remove-Item Env:\MELONDS_NSML_RAM_DUMP_DIR -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_RAM_DUMP_FRAMES -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_RAM_DUMP_INTERVAL -ErrorAction SilentlyContinue
    }
    if ($StateSaveDir -and $StateSaveFrame -gt 0) {
        $roleStateSaveDir = Join-Path $StateSaveDir $Role
        New-Item -ItemType Directory -Force -Path $roleStateSaveDir | Out-Null
        $env:MELONDS_NSML_STATE_SAVE_DIR = (Resolve-Path $roleStateSaveDir).Path
        $env:MELONDS_NSML_STATE_SAVE_FRAME = "$StateSaveFrame"
    } else {
        Remove-Item Env:\MELONDS_NSML_STATE_SAVE_DIR -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_STATE_SAVE_FRAME -ErrorAction SilentlyContinue
    }
    if ($StateLoadDir) {
        $roleStateLoadDir = Join-Path $StateLoadDir $Role
        if (Test-Path $roleStateLoadDir) {
            $env:MELONDS_NSML_STATE_LOAD_DIR = (Resolve-Path $roleStateLoadDir).Path
        } else {
            $env:MELONDS_NSML_STATE_LOAD_DIR = (Resolve-Path $StateLoadDir).Path
        }
        if ($StateLoadFrame -lt 0) {
            $env:MELONDS_NSML_STATE_LOAD_FRAME = "1"
        } else {
            $env:MELONDS_NSML_STATE_LOAD_FRAME = "$StateLoadFrame"
        }
    } else {
        Remove-Item Env:\MELONDS_NSML_STATE_LOAD_DIR -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_STATE_LOAD_FRAME -ErrorAction SilentlyContinue
    }
    if ($VsStarSnapFrame -gt 0) {
        $env:MELONDS_NSML_VS_STAR_SNAP_FRAME = "$VsStarSnapFrame"
        $env:MELONDS_NSML_VS_STAR_SNAP_PLAYER_SLOT = "$VsStarSnapPlayerSlot"
    } else {
        Remove-Item Env:\MELONDS_NSML_VS_STAR_SNAP_FRAME -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_VS_STAR_SNAP_PLAYER_SLOT -ErrorAction SilentlyContinue
    }
    if ($PlayerSnapToStarFrame -gt 0) {
        $env:MELONDS_NSML_PLAYER_SNAP_TO_STAR_FRAME = "$PlayerSnapToStarFrame"
        $env:MELONDS_NSML_PLAYER_SNAP_TO_STAR_SLOT = "$PlayerSnapToStarSlot"
    } else {
        Remove-Item Env:\MELONDS_NSML_PLAYER_SNAP_TO_STAR_FRAME -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_PLAYER_SNAP_TO_STAR_SLOT -ErrorAction SilentlyContinue
    }
    if ($PlayerStickToStarStartFrame -gt 0) {
        $env:MELONDS_NSML_PLAYER_STICK_TO_STAR_START_FRAME = "$PlayerStickToStarStartFrame"
        if ($PlayerStickToStarEndFrame -gt 0) {
            $env:MELONDS_NSML_PLAYER_STICK_TO_STAR_END_FRAME = "$PlayerStickToStarEndFrame"
        } else {
            $env:MELONDS_NSML_PLAYER_STICK_TO_STAR_END_FRAME = "$PlayerStickToStarStartFrame"
        }
        $env:MELONDS_NSML_PLAYER_STICK_TO_STAR_SLOT = "$PlayerStickToStarSlot"
    } else {
        Remove-Item Env:\MELONDS_NSML_PLAYER_STICK_TO_STAR_START_FRAME -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_PLAYER_STICK_TO_STAR_END_FRAME -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_PLAYER_STICK_TO_STAR_SLOT -ErrorAction SilentlyContinue
    }
    if ($LanMPTrace) {
        $env:MELONDS_NSML_LANMP_TRACE = $LanMPTracePath
        $env:MELONDS_NSML_LANMP_TRACE_DUMP_LEN = "$LanMPTraceDumpLen"
    } else {
        Remove-Item Env:\MELONDS_NSML_LANMP_TRACE -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_LANMP_TRACE_DUMP_LEN -ErrorAction SilentlyContinue
    }
    if ($NetRandomValue) {
        $env:MELONDS_NSML_NET_RANDOM_VALUE = $NetRandomValue
        $env:MELONDS_NSML_NET_RANDOM_FRAME = "$NetRandomFrame"
        if ($NetRandomAuto) {
            $env:MELONDS_NSML_NET_RANDOM_AUTO = "1"
        } else {
            Remove-Item Env:\MELONDS_NSML_NET_RANDOM_AUTO -ErrorAction SilentlyContinue
        }
    } else {
        Remove-Item Env:\MELONDS_NSML_NET_RANDOM_VALUE -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_NET_RANDOM_FRAME -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_NET_RANDOM_AUTO -ErrorAction SilentlyContinue
    }
    if ($ForcePlayerCount) {
        $env:MELONDS_NSML_FORCE_PLAYER_COUNT = "1"
        if ($ForcePlayerCountHostOnly) { $env:MELONDS_NSML_FORCE_PLAYER_COUNT_HOST_ONLY = "1" } else { Remove-Item Env:\MELONDS_NSML_FORCE_PLAYER_COUNT_HOST_ONLY -ErrorAction SilentlyContinue }
        if ($ForcePlayerCountClientOnly) { $env:MELONDS_NSML_FORCE_PLAYER_COUNT_CLIENT_ONLY = "1" } else { Remove-Item Env:\MELONDS_NSML_FORCE_PLAYER_COUNT_CLIENT_ONLY -ErrorAction SilentlyContinue }
        $env:MELONDS_NSML_FORCE_PLAYER_COUNT_START_FRAME = "$ForcePlayerCountStartFrame"
        $env:MELONDS_NSML_FORCE_PLAYER_COUNT_END_FRAME = "$ForcePlayerCountEndFrame"
        $env:MELONDS_NSML_FORCE_PLAYER_COUNT_VALUE = "$ForcePlayerCountValue"
    } else {
        Remove-Item Env:\MELONDS_NSML_FORCE_PLAYER_COUNT -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_FORCE_PLAYER_COUNT_HOST_ONLY -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_FORCE_PLAYER_COUNT_CLIENT_ONLY -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_FORCE_PLAYER_COUNT_START_FRAME -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_FORCE_PLAYER_COUNT_END_FRAME -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_FORCE_PLAYER_COUNT_VALUE -ErrorAction SilentlyContinue
    }
    if ($ForceStageSceneRuntimeWords) {
        $env:MELONDS_NSML_FORCE_STAGE_SCENE_RUNTIME_WORDS = "1"
        if ($ForceStageSceneRuntimeWordsHostOnly) { $env:MELONDS_NSML_FORCE_STAGE_SCENE_RUNTIME_WORDS_HOST_ONLY = "1" } else { Remove-Item Env:\MELONDS_NSML_FORCE_STAGE_SCENE_RUNTIME_WORDS_HOST_ONLY -ErrorAction SilentlyContinue }
        if ($ForceStageSceneRuntimeWordsClientOnly) { $env:MELONDS_NSML_FORCE_STAGE_SCENE_RUNTIME_WORDS_CLIENT_ONLY = "1" } else { Remove-Item Env:\MELONDS_NSML_FORCE_STAGE_SCENE_RUNTIME_WORDS_CLIENT_ONLY -ErrorAction SilentlyContinue }
        $env:MELONDS_NSML_FORCE_STAGE_SCENE_RUNTIME_WORDS_START_FRAME = "$ForceStageSceneRuntimeWordsStartFrame"
        $env:MELONDS_NSML_FORCE_STAGE_SCENE_RUNTIME_WORDS_END_FRAME = "$ForceStageSceneRuntimeWordsEndFrame"
        $env:MELONDS_NSML_FORCE_STAGE_SCENE_WORD154 = "$ForceStageSceneWord154"
        $env:MELONDS_NSML_FORCE_STAGE_SCENE_WORD160 = "$ForceStageSceneWord160"
    } else {
        Remove-Item Env:\MELONDS_NSML_FORCE_STAGE_SCENE_RUNTIME_WORDS -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_FORCE_STAGE_SCENE_RUNTIME_WORDS_HOST_ONLY -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_FORCE_STAGE_SCENE_RUNTIME_WORDS_CLIENT_ONLY -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_FORCE_STAGE_SCENE_RUNTIME_WORDS_START_FRAME -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_FORCE_STAGE_SCENE_RUNTIME_WORDS_END_FRAME -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_FORCE_STAGE_SCENE_WORD154 -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_FORCE_STAGE_SCENE_WORD160 -ErrorAction SilentlyContinue
    }
    if ($ForceStageCameraSlot) {
        $env:MELONDS_NSML_FORCE_STAGE_CAMERA_SLOT = "1"
        $env:MELONDS_NSML_FORCE_STAGE_CAMERA_SLOT_START_FRAME = "$ForceStageCameraSlotStartFrame"
        $env:MELONDS_NSML_FORCE_STAGE_CAMERA_SLOT_END_FRAME = "$ForceStageCameraSlotEndFrame"
        $env:MELONDS_NSML_FORCE_STAGE_CAMERA_SLOT_SOURCE = "$ForceStageCameraSlotSource"
        $env:MELONDS_NSML_FORCE_STAGE_CAMERA_SLOT_DEST = "$ForceStageCameraSlotDest"
        if ($ForceStageCameraSlotVerticalOnly) { $env:MELONDS_NSML_FORCE_STAGE_CAMERA_SLOT_VERTICAL_ONLY = "1" } else { Remove-Item Env:\MELONDS_NSML_FORCE_STAGE_CAMERA_SLOT_VERTICAL_ONLY -ErrorAction SilentlyContinue }
    } else {
        Remove-Item Env:\MELONDS_NSML_FORCE_STAGE_CAMERA_SLOT -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_FORCE_STAGE_CAMERA_SLOT_VERTICAL_ONLY -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_FORCE_STAGE_CAMERA_SLOT_START_FRAME -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_FORCE_STAGE_CAMERA_SLOT_END_FRAME -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_FORCE_STAGE_CAMERA_SLOT_SOURCE -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_FORCE_STAGE_CAMERA_SLOT_DEST -ErrorAction SilentlyContinue
    }
    if ($ForceStageCameraObjectX) {
        $env:MELONDS_NSML_FORCE_STAGE_CAMERA_OBJECT_X = "1"
        $env:MELONDS_NSML_FORCE_STAGE_CAMERA_OBJECT_X_START_FRAME = "$ForceStageCameraObjectXStartFrame"
        $env:MELONDS_NSML_FORCE_STAGE_CAMERA_OBJECT_X_END_FRAME = "$ForceStageCameraObjectXEndFrame"
        $env:MELONDS_NSML_FORCE_STAGE_CAMERA_OBJECT_X_VALUE = "$ForceStageCameraObjectXValue"
        $env:MELONDS_NSML_FORCE_STAGE_CAMERA_OBJECT_X_SLOT = "$ForceStageCameraObjectXSlot"
        if ($ForceStageCameraObjectZValue -ne "") { $env:MELONDS_NSML_FORCE_STAGE_CAMERA_OBJECT_Z_VALUE = "$ForceStageCameraObjectZValue" } else { Remove-Item Env:\MELONDS_NSML_FORCE_STAGE_CAMERA_OBJECT_Z_VALUE -ErrorAction SilentlyContinue }
        if ($ForceStageCameraObjectXWriteDisplay) { $env:MELONDS_NSML_FORCE_STAGE_CAMERA_OBJECT_X_WRITE_DISPLAY = "1" } else { Remove-Item Env:\MELONDS_NSML_FORCE_STAGE_CAMERA_OBJECT_X_WRITE_DISPLAY -ErrorAction SilentlyContinue }
        if ($ForceStageCameraObjectXWriteSlot) { $env:MELONDS_NSML_FORCE_STAGE_CAMERA_OBJECT_X_WRITE_SLOT = "1" } else { Remove-Item Env:\MELONDS_NSML_FORCE_STAGE_CAMERA_OBJECT_X_WRITE_SLOT -ErrorAction SilentlyContinue }
    } else {
        Remove-Item Env:\MELONDS_NSML_FORCE_STAGE_CAMERA_OBJECT_X -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_FORCE_STAGE_CAMERA_OBJECT_X_START_FRAME -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_FORCE_STAGE_CAMERA_OBJECT_X_END_FRAME -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_FORCE_STAGE_CAMERA_OBJECT_X_VALUE -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_FORCE_STAGE_CAMERA_OBJECT_Z_VALUE -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_FORCE_STAGE_CAMERA_OBJECT_X_WRITE_DISPLAY -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_FORCE_STAGE_CAMERA_OBJECT_X_WRITE_SLOT -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_FORCE_STAGE_CAMERA_OBJECT_X_SLOT -ErrorAction SilentlyContinue
    }
    if ($ForceStageSceneActive) {
        $env:MELONDS_NSML_FORCE_STAGE_SCENE_ACTIVE = "1"
        if ($ForceStageSceneActiveHostOnly) { $env:MELONDS_NSML_FORCE_STAGE_SCENE_ACTIVE_HOST_ONLY = "1" } else { Remove-Item Env:\MELONDS_NSML_FORCE_STAGE_SCENE_ACTIVE_HOST_ONLY -ErrorAction SilentlyContinue }
        if ($ForceStageSceneActiveClientOnly) { $env:MELONDS_NSML_FORCE_STAGE_SCENE_ACTIVE_CLIENT_ONLY = "1" } else { Remove-Item Env:\MELONDS_NSML_FORCE_STAGE_SCENE_ACTIVE_CLIENT_ONLY -ErrorAction SilentlyContinue }
        $env:MELONDS_NSML_FORCE_STAGE_SCENE_ACTIVE_START_FRAME = "$ForceStageSceneActiveStartFrame"
        $env:MELONDS_NSML_FORCE_STAGE_SCENE_ACTIVE_END_FRAME = "$ForceStageSceneActiveEndFrame"
    } else {
        Remove-Item Env:\MELONDS_NSML_FORCE_STAGE_SCENE_ACTIVE -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_FORCE_STAGE_SCENE_ACTIVE_HOST_ONLY -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_FORCE_STAGE_SCENE_ACTIVE_CLIENT_ONLY -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_FORCE_STAGE_SCENE_ACTIVE_START_FRAME -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_FORCE_STAGE_SCENE_ACTIVE_END_FRAME -ErrorAction SilentlyContinue
    }
    if ($ForceStageActorFreezeFlag) {
        $env:MELONDS_NSML_FORCE_STAGE_ACTOR_FREEZE_FLAG = "1"
        if ($ForceStageActorFreezeFlagHostOnly) { $env:MELONDS_NSML_FORCE_STAGE_ACTOR_FREEZE_FLAG_HOST_ONLY = "1" } else { Remove-Item Env:\MELONDS_NSML_FORCE_STAGE_ACTOR_FREEZE_FLAG_HOST_ONLY -ErrorAction SilentlyContinue }
        if ($ForceStageActorFreezeFlagClientOnly) { $env:MELONDS_NSML_FORCE_STAGE_ACTOR_FREEZE_FLAG_CLIENT_ONLY = "1" } else { Remove-Item Env:\MELONDS_NSML_FORCE_STAGE_ACTOR_FREEZE_FLAG_CLIENT_ONLY -ErrorAction SilentlyContinue }
        $env:MELONDS_NSML_FORCE_STAGE_ACTOR_FREEZE_FLAG_START_FRAME = "$ForceStageActorFreezeFlagStartFrame"
        $env:MELONDS_NSML_FORCE_STAGE_ACTOR_FREEZE_FLAG_END_FRAME = "$ForceStageActorFreezeFlagEndFrame"
        $env:MELONDS_NSML_FORCE_STAGE_ACTOR_FREEZE_FLAG_VALUE = "$ForceStageActorFreezeFlagValue"
    } else {
        Remove-Item Env:\MELONDS_NSML_FORCE_STAGE_ACTOR_FREEZE_FLAG -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_FORCE_STAGE_ACTOR_FREEZE_FLAG_HOST_ONLY -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_FORCE_STAGE_ACTOR_FREEZE_FLAG_CLIENT_ONLY -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_FORCE_STAGE_ACTOR_FREEZE_FLAG_START_FRAME -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_FORCE_STAGE_ACTOR_FREEZE_FLAG_END_FRAME -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_FORCE_STAGE_ACTOR_FREEZE_FLAG_VALUE -ErrorAction SilentlyContinue
    }
    if ($ForcePlayerDeathCounters) {
        $env:MELONDS_NSML_FORCE_PLAYER_DEATH_COUNTERS = "1"
        if ($ForcePlayerDeathCountersHostOnly) { $env:MELONDS_NSML_FORCE_PLAYER_DEATH_COUNTERS_HOST_ONLY = "1" } else { Remove-Item Env:\MELONDS_NSML_FORCE_PLAYER_DEATH_COUNTERS_HOST_ONLY -ErrorAction SilentlyContinue }
        if ($ForcePlayerDeathCountersClientOnly) { $env:MELONDS_NSML_FORCE_PLAYER_DEATH_COUNTERS_CLIENT_ONLY = "1" } else { Remove-Item Env:\MELONDS_NSML_FORCE_PLAYER_DEATH_COUNTERS_CLIENT_ONLY -ErrorAction SilentlyContinue }
        $env:MELONDS_NSML_FORCE_PLAYER_DEATH_COUNTERS_START_FRAME = "$ForcePlayerDeathCountersStartFrame"
        $env:MELONDS_NSML_FORCE_PLAYER_DEATH_COUNTERS_END_FRAME = "$ForcePlayerDeathCountersEndFrame"
        $env:MELONDS_NSML_FORCE_PLAYER_DEATH_COUNTER0 = "$ForcePlayerDeathCounter0"
        $env:MELONDS_NSML_FORCE_PLAYER_DEATH_COUNTER1 = "$ForcePlayerDeathCounter1"
    } else {
        Remove-Item Env:\MELONDS_NSML_FORCE_PLAYER_DEATH_COUNTERS -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_FORCE_PLAYER_DEATH_COUNTERS_HOST_ONLY -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_FORCE_PLAYER_DEATH_COUNTERS_CLIENT_ONLY -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_FORCE_PLAYER_DEATH_COUNTERS_START_FRAME -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_FORCE_PLAYER_DEATH_COUNTERS_END_FRAME -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_FORCE_PLAYER_DEATH_COUNTER0 -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_FORCE_PLAYER_DEATH_COUNTER1 -ErrorAction SilentlyContinue
    }
    if ($ForcePlayerLives) {
        $env:MELONDS_NSML_FORCE_PLAYER_LIVES = "1"
        $env:MELONDS_NSML_FORCE_PLAYER_LIFE0 = "$ForcePlayerLife0"
        $env:MELONDS_NSML_FORCE_PLAYER_LIFE1 = "$ForcePlayerLife1"
    } else {
        Remove-Item Env:\MELONDS_NSML_FORCE_PLAYER_LIVES -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_FORCE_PLAYER_LIFE0 -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_FORCE_PLAYER_LIFE1 -ErrorAction SilentlyContinue
    }
    if ($ForcePlayerInventoryPowerups) {
        $env:MELONDS_NSML_FORCE_PLAYER_INVENTORY_POWERUPS = "1"
        $env:MELONDS_NSML_FORCE_PLAYER_INVENTORY_POWERUPS_START_FRAME = "$ForcePlayerInventoryPowerupsStartFrame"
        $env:MELONDS_NSML_FORCE_PLAYER_INVENTORY_POWERUPS_END_FRAME = "$ForcePlayerInventoryPowerupsEndFrame"
        $env:MELONDS_NSML_FORCE_PLAYER_INVENTORY_POWERUP0 = "$ForcePlayerInventoryPowerup0"
        $env:MELONDS_NSML_FORCE_PLAYER_INVENTORY_POWERUP1 = "$ForcePlayerInventoryPowerup1"
    } else {
        Remove-Item Env:\MELONDS_NSML_FORCE_PLAYER_INVENTORY_POWERUPS -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_FORCE_PLAYER_INVENTORY_POWERUPS_START_FRAME -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_FORCE_PLAYER_INVENTORY_POWERUPS_END_FRAME -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_FORCE_PLAYER_INVENTORY_POWERUP0 -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_FORCE_PLAYER_INVENTORY_POWERUP1 -ErrorAction SilentlyContinue
    }
    if ($ForcePlayerStarCounters) {
        $env:MELONDS_NSML_FORCE_PLAYER_STAR_COUNTERS = "1"
        $env:MELONDS_NSML_FORCE_PLAYER_STAR_COUNTERS_START_FRAME = "$ForcePlayerStarCountersStartFrame"
        $env:MELONDS_NSML_FORCE_PLAYER_STAR_COUNTERS_END_FRAME = "$ForcePlayerStarCountersEndFrame"
        $env:MELONDS_NSML_FORCE_PLAYER_BATTLE_STARS0 = "$ForcePlayerBattleStars0"
        $env:MELONDS_NSML_FORCE_PLAYER_BATTLE_STARS1 = "$ForcePlayerBattleStars1"
        $env:MELONDS_NSML_FORCE_PLAYER_DISPLAYED_STARS0 = "$ForcePlayerDisplayedStars0"
        $env:MELONDS_NSML_FORCE_PLAYER_DISPLAYED_STARS1 = "$ForcePlayerDisplayedStars1"
        $env:MELONDS_NSML_FORCE_PLAYER_COLLECTED_STARS0 = "$ForcePlayerCollectedStars0"
        $env:MELONDS_NSML_FORCE_PLAYER_COLLECTED_STARS1 = "$ForcePlayerCollectedStars1"
    } else {
        Remove-Item Env:\MELONDS_NSML_FORCE_PLAYER_STAR_COUNTERS -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_FORCE_PLAYER_STAR_COUNTERS_START_FRAME -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_FORCE_PLAYER_STAR_COUNTERS_END_FRAME -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_FORCE_PLAYER_BATTLE_STARS0 -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_FORCE_PLAYER_BATTLE_STARS1 -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_FORCE_PLAYER_DISPLAYED_STARS0 -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_FORCE_PLAYER_DISPLAYED_STARS1 -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_FORCE_PLAYER_COLLECTED_STARS0 -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_FORCE_PLAYER_COLLECTED_STARS1 -ErrorAction SilentlyContinue
    }
    if ($TracePlayerLifeChanges) {
        $env:MELONDS_NSML_TRACE_PLAYER_LIFE_CHANGES = "1"
    } else {
        Remove-Item Env:\MELONDS_NSML_TRACE_PLAYER_LIFE_CHANGES -ErrorAction SilentlyContinue
    }
    if ($RenderCameraAlias) {
        $env:MELONDS_NSML_RENDER_CAMERA_ALIAS = "1"
        if ($RenderCameraAliasAllRoles) { $env:MELONDS_NSML_RENDER_CAMERA_ALIAS_ALL_ROLES = "1" } else { Remove-Item Env:\MELONDS_NSML_RENDER_CAMERA_ALIAS_ALL_ROLES -ErrorAction SilentlyContinue }
        $env:MELONDS_NSML_RENDER_CAMERA_ALIAS_SOURCE_PLAYER = "$RenderCameraAliasSourcePlayer"
        $env:MELONDS_NSML_RENDER_CAMERA_ALIAS_DEST_PLAYER = "$RenderCameraAliasDestPlayer"
        $env:MELONDS_NSML_RENDER_CAMERA_ALIAS_START_FRAME = "$RenderCameraAliasStartFrame"
        $env:MELONDS_NSML_RENDER_CAMERA_ALIAS_END_FRAME = "$RenderCameraAliasEndFrame"
    } else {
        Remove-Item Env:\MELONDS_NSML_RENDER_CAMERA_ALIAS -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_RENDER_CAMERA_ALIAS_ALL_ROLES -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_RENDER_CAMERA_ALIAS_SOURCE_PLAYER -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_RENDER_CAMERA_ALIAS_DEST_PLAYER -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_RENDER_CAMERA_ALIAS_START_FRAME -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_RENDER_CAMERA_ALIAS_END_FRAME -ErrorAction SilentlyContinue
    }
    if ($ForceCameraFocusLoopCount) {
        $env:MELONDS_NSML_FORCE_CAMERA_FOCUS_LOOP_COUNT = "1"
        if ($ForceCameraFocusLoopCountHostOnly) { $env:MELONDS_NSML_FORCE_CAMERA_FOCUS_LOOP_COUNT_HOST_ONLY = "1" } else { Remove-Item Env:\MELONDS_NSML_FORCE_CAMERA_FOCUS_LOOP_COUNT_HOST_ONLY -ErrorAction SilentlyContinue }
        if ($ForceCameraFocusLoopCountClientOnly) { $env:MELONDS_NSML_FORCE_CAMERA_FOCUS_LOOP_COUNT_CLIENT_ONLY = "1" } else { Remove-Item Env:\MELONDS_NSML_FORCE_CAMERA_FOCUS_LOOP_COUNT_CLIENT_ONLY -ErrorAction SilentlyContinue }
        $env:MELONDS_NSML_FORCE_CAMERA_FOCUS_LOOP_COUNT_VALUE = "$ForceCameraFocusLoopCountValue"
        $env:MELONDS_NSML_FORCE_CAMERA_FOCUS_LOOP_COUNT_START_FRAME = "$ForceCameraFocusLoopCountStartFrame"
        $env:MELONDS_NSML_FORCE_CAMERA_FOCUS_LOOP_COUNT_END_FRAME = "$ForceCameraFocusLoopCountEndFrame"
    } else {
        Remove-Item Env:\MELONDS_NSML_FORCE_CAMERA_FOCUS_LOOP_COUNT -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_FORCE_CAMERA_FOCUS_LOOP_COUNT_HOST_ONLY -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_FORCE_CAMERA_FOCUS_LOOP_COUNT_CLIENT_ONLY -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_FORCE_CAMERA_FOCUS_LOOP_COUNT_VALUE -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_FORCE_CAMERA_FOCUS_LOOP_COUNT_START_FRAME -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_FORCE_CAMERA_FOCUS_LOOP_COUNT_END_FRAME -ErrorAction SilentlyContinue
    }
    if ($ForceStageFXSettings) {
        $env:MELONDS_NSML_FORCE_STAGEFX_SETTINGS = "1"
        if ($ForceStageFXSettingsHostOnly) { $env:MELONDS_NSML_FORCE_STAGEFX_SETTINGS_HOST_ONLY = "1" } else { Remove-Item Env:\MELONDS_NSML_FORCE_STAGEFX_SETTINGS_HOST_ONLY -ErrorAction SilentlyContinue }
        if ($ForceStageFXSettingsClientOnly) { $env:MELONDS_NSML_FORCE_STAGEFX_SETTINGS_CLIENT_ONLY = "1" } else { Remove-Item Env:\MELONDS_NSML_FORCE_STAGEFX_SETTINGS_CLIENT_ONLY -ErrorAction SilentlyContinue }
        $env:MELONDS_NSML_FORCE_STAGEFX_SETTINGS_START_FRAME = "$ForceStageFXSettingsStartFrame"
        $env:MELONDS_NSML_FORCE_STAGEFX_SETTINGS_END_FRAME = "$ForceStageFXSettingsEndFrame"
        $env:MELONDS_NSML_FORCE_STAGEFX_SETTINGS_VALUE = "$ForceStageFXSettingsValue"
    } else {
        Remove-Item Env:\MELONDS_NSML_FORCE_STAGEFX_SETTINGS -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_FORCE_STAGEFX_SETTINGS_HOST_ONLY -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_FORCE_STAGEFX_SETTINGS_CLIENT_ONLY -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_FORCE_STAGEFX_SETTINGS_START_FRAME -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_FORCE_STAGEFX_SETTINGS_END_FRAME -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_FORCE_STAGEFX_SETTINGS_VALUE -ErrorAction SilentlyContinue
    }
    if ($TracePlayerLifeCalls) {
        $env:MELONDS_NSML_TRACE_PLAYER_LIFE_CALLS = "1"
    } else {
        Remove-Item Env:\MELONDS_NSML_TRACE_PLAYER_LIFE_CALLS -ErrorAction SilentlyContinue
    }
    if ($TracePlayerDefeated) {
        $env:MELONDS_NSML_TRACE_PLAYER_DEFEATED = "1"
    } else {
        Remove-Item Env:\MELONDS_NSML_TRACE_PLAYER_DEFEATED -ErrorAction SilentlyContinue
    }
    if ($TracePlayerRender) {
        $env:MELONDS_NSML_TRACE_PLAYER_RENDER = "1"
        if ($TracePlayerRenderStartFrame -gt 0) { $env:MELONDS_NSML_TRACE_PLAYER_RENDER_START_FRAME = "$TracePlayerRenderStartFrame" } else { Remove-Item Env:\MELONDS_NSML_TRACE_PLAYER_RENDER_START_FRAME -ErrorAction SilentlyContinue }
        if ($TracePlayerRenderEndFrame -gt 0) { $env:MELONDS_NSML_TRACE_PLAYER_RENDER_END_FRAME = "$TracePlayerRenderEndFrame" } else { Remove-Item Env:\MELONDS_NSML_TRACE_PLAYER_RENDER_END_FRAME -ErrorAction SilentlyContinue }
    } else {
        Remove-Item Env:\MELONDS_NSML_TRACE_PLAYER_RENDER -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_TRACE_PLAYER_RENDER_START_FRAME -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_TRACE_PLAYER_RENDER_END_FRAME -ErrorAction SilentlyContinue
    }
    if ($TraceStageCamera) {
        $env:MELONDS_NSML_TRACE_STAGE_CAMERA = "1"
        $env:MELONDS_NSML_TRACE_STAGE_CAMERA_START_FRAME = "$TraceStageCameraStartFrame"
        $env:MELONDS_NSML_TRACE_STAGE_CAMERA_INTERVAL = "$TraceStageCameraInterval"
        if ($TraceStageCameraEndFrame -gt 0) { $env:MELONDS_NSML_TRACE_STAGE_CAMERA_END_FRAME = "$TraceStageCameraEndFrame" } else { Remove-Item Env:\MELONDS_NSML_TRACE_STAGE_CAMERA_END_FRAME -ErrorAction SilentlyContinue }
    } else {
        Remove-Item Env:\MELONDS_NSML_TRACE_STAGE_CAMERA -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_TRACE_STAGE_CAMERA_START_FRAME -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_TRACE_STAGE_CAMERA_END_FRAME -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_TRACE_STAGE_CAMERA_INTERVAL -ErrorAction SilentlyContinue
    }
    if ($ForcePlayerSignalUnlock) {
        $env:MELONDS_NSML_FORCE_PLAYER_SIGNAL_UNLOCK = "1"
        if ($ForcePlayerSignalUnlockHostOnly) { $env:MELONDS_NSML_FORCE_PLAYER_SIGNAL_UNLOCK_HOST_ONLY = "1" } else { Remove-Item Env:\MELONDS_NSML_FORCE_PLAYER_SIGNAL_UNLOCK_HOST_ONLY -ErrorAction SilentlyContinue }
        if ($ForcePlayerSignalUnlockClientOnly) { $env:MELONDS_NSML_FORCE_PLAYER_SIGNAL_UNLOCK_CLIENT_ONLY = "1" } else { Remove-Item Env:\MELONDS_NSML_FORCE_PLAYER_SIGNAL_UNLOCK_CLIENT_ONLY -ErrorAction SilentlyContinue }
        $env:MELONDS_NSML_FORCE_PLAYER_SIGNAL_UNLOCK_START_FRAME = "$ForcePlayerSignalUnlockStartFrame"
        $env:MELONDS_NSML_FORCE_PLAYER_SIGNAL_UNLOCK_END_FRAME = "$ForcePlayerSignalUnlockEndFrame"
    } else {
        Remove-Item Env:\MELONDS_NSML_FORCE_PLAYER_SIGNAL_UNLOCK -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_FORCE_PLAYER_SIGNAL_UNLOCK_HOST_ONLY -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_FORCE_PLAYER_SIGNAL_UNLOCK_CLIENT_ONLY -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_FORCE_PLAYER_SIGNAL_UNLOCK_START_FRAME -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_FORCE_PLAYER_SIGNAL_UNLOCK_END_FRAME -ErrorAction SilentlyContinue
    }
    if ($ForceStageSceneStartGate) {
        $env:MELONDS_NSML_FORCE_STAGE_SCENE_START_GATE = "1"
        if ($ForceStageSceneStartGateHostOnly) { $env:MELONDS_NSML_FORCE_STAGE_SCENE_START_GATE_HOST_ONLY = "1" } else { Remove-Item Env:\MELONDS_NSML_FORCE_STAGE_SCENE_START_GATE_HOST_ONLY -ErrorAction SilentlyContinue }
        if ($ForceStageSceneStartGateClientOnly) { $env:MELONDS_NSML_FORCE_STAGE_SCENE_START_GATE_CLIENT_ONLY = "1" } else { Remove-Item Env:\MELONDS_NSML_FORCE_STAGE_SCENE_START_GATE_CLIENT_ONLY -ErrorAction SilentlyContinue }
        if ($ForceStageSceneFadeReady) { $env:MELONDS_NSML_FORCE_STAGE_SCENE_FADE_READY = "1" } else { Remove-Item Env:\MELONDS_NSML_FORCE_STAGE_SCENE_FADE_READY -ErrorAction SilentlyContinue }
        if ($ForceStageSceneInputLatch) { $env:MELONDS_NSML_FORCE_STAGE_SCENE_INPUT_LATCH = "1" } else { Remove-Item Env:\MELONDS_NSML_FORCE_STAGE_SCENE_INPUT_LATCH -ErrorAction SilentlyContinue }
        if ($ForceNetLocalAid -ge 0) {
            $env:MELONDS_NSML_FORCE_NET_LOCAL_AID = "$ForceNetLocalAid"
            $env:MELONDS_NSML_FORCE_NET_LOCAL_AID_START_FRAME = "$ForceNetLocalAidStartFrame"
            $env:MELONDS_NSML_FORCE_NET_LOCAL_AID_END_FRAME = "$ForceNetLocalAidEndFrame"
        } else {
            Remove-Item Env:\MELONDS_NSML_FORCE_NET_LOCAL_AID -ErrorAction SilentlyContinue
            Remove-Item Env:\MELONDS_NSML_FORCE_NET_LOCAL_AID_START_FRAME -ErrorAction SilentlyContinue
            Remove-Item Env:\MELONDS_NSML_FORCE_NET_LOCAL_AID_END_FRAME -ErrorAction SilentlyContinue
        }
        if ($ForceWifiCommunicatingCount -ge 0) {
            $env:MELONDS_NSML_FORCE_WIFI_COMMUNICATING_COUNT = "$ForceWifiCommunicatingCount"
            $env:MELONDS_NSML_FORCE_WIFI_COMMUNICATING_START_FRAME = "$ForceWifiCommunicatingStartFrame"
            $env:MELONDS_NSML_FORCE_WIFI_COMMUNICATING_END_FRAME = "$ForceWifiCommunicatingEndFrame"
        } else {
            Remove-Item Env:\MELONDS_NSML_FORCE_WIFI_COMMUNICATING_COUNT -ErrorAction SilentlyContinue
            Remove-Item Env:\MELONDS_NSML_FORCE_WIFI_COMMUNICATING_START_FRAME -ErrorAction SilentlyContinue
            Remove-Item Env:\MELONDS_NSML_FORCE_WIFI_COMMUNICATING_END_FRAME -ErrorAction SilentlyContinue
        }
        if ($PacketBridgeArmOnly) {
            $env:MELONDS_NSML_PACKET_BRIDGE_ARM_ONLY = "1"
            if ($PacketBridgeTrace) {
                $env:MELONDS_NSML_PACKET_BRIDGE_TRACE = "1"
                $env:MELONDS_NSML_PACKET_REPLAY_LOG = "$Stdout.packet-replay.csv"
            }
        } else { Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_ARM_ONLY -ErrorAction SilentlyContinue }
        if ($ScriptRemotePacket) {
            $env:MELONDS_NSML_SCRIPT_REMOTE_PACKET = "1"
            $env:MELONDS_NSML_SCRIPT_REMOTE_PACKET_PLAYER = "$ScriptRemotePacketPlayer"
            $env:MELONDS_NSML_SCRIPT_REMOTE_PACKET_INPUT_INSTANCE = "$ScriptRemotePacketInputInstance"
            if ($ScriptRemotePacketInputScript) {
                $env:MELONDS_NSML_SCRIPT_REMOTE_PACKET_INPUT_SCRIPT = (Resolve-Path $ScriptRemotePacketInputScript).Path
            } elseif ($Role -eq "host") {
                $env:MELONDS_NSML_SCRIPT_REMOTE_PACKET_INPUT_SCRIPT = (Resolve-Path $clientInput).Path
            } else {
                $env:MELONDS_NSML_SCRIPT_REMOTE_PACKET_INPUT_SCRIPT = (Resolve-Path $hostInput).Path
            }
            $env:MELONDS_NSML_SCRIPT_REMOTE_PACKET_START_FRAME = "$ScriptRemotePacketStartFrame"
            $env:MELONDS_NSML_SCRIPT_REMOTE_PACKET_END_FRAME = "$ScriptRemotePacketEndFrame"
        } else {
            Remove-Item Env:\MELONDS_NSML_SCRIPT_REMOTE_PACKET -ErrorAction SilentlyContinue
            Remove-Item Env:\MELONDS_NSML_SCRIPT_REMOTE_PACKET_PLAYER -ErrorAction SilentlyContinue
            Remove-Item Env:\MELONDS_NSML_SCRIPT_REMOTE_PACKET_INPUT_INSTANCE -ErrorAction SilentlyContinue
            Remove-Item Env:\MELONDS_NSML_SCRIPT_REMOTE_PACKET_INPUT_SCRIPT -ErrorAction SilentlyContinue
            Remove-Item Env:\MELONDS_NSML_SCRIPT_REMOTE_PACKET_START_FRAME -ErrorAction SilentlyContinue
            Remove-Item Env:\MELONDS_NSML_SCRIPT_REMOTE_PACKET_END_FRAME -ErrorAction SilentlyContinue
        }
        $env:MELONDS_NSML_FORCE_STAGE_SCENE_START_GATE_START_FRAME = "$ForceStageSceneStartGateStartFrame"
        $env:MELONDS_NSML_FORCE_STAGE_SCENE_START_GATE_END_FRAME = "$ForceStageSceneStartGateEndFrame"
        $env:MELONDS_NSML_FORCE_STAGE_SCENE_START_GATE_VALUE = "$ForceStageSceneStartGateValue"
    } else {
        Remove-Item Env:\MELONDS_NSML_FORCE_STAGE_SCENE_START_GATE -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_FORCE_STAGE_SCENE_START_GATE_HOST_ONLY -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_FORCE_STAGE_SCENE_START_GATE_CLIENT_ONLY -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_FORCE_STAGE_SCENE_FADE_READY -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_FORCE_STAGE_SCENE_INPUT_LATCH -ErrorAction SilentlyContinue
        if ($ForceNetLocalAid -ge 0) {
            $env:MELONDS_NSML_FORCE_NET_LOCAL_AID = "$ForceNetLocalAid"
            $env:MELONDS_NSML_FORCE_NET_LOCAL_AID_START_FRAME = "$ForceNetLocalAidStartFrame"
            $env:MELONDS_NSML_FORCE_NET_LOCAL_AID_END_FRAME = "$ForceNetLocalAidEndFrame"
        } else {
            Remove-Item Env:\MELONDS_NSML_FORCE_NET_LOCAL_AID -ErrorAction SilentlyContinue
            Remove-Item Env:\MELONDS_NSML_FORCE_NET_LOCAL_AID_START_FRAME -ErrorAction SilentlyContinue
            Remove-Item Env:\MELONDS_NSML_FORCE_NET_LOCAL_AID_END_FRAME -ErrorAction SilentlyContinue
        }
        if ($ForceWifiCommunicatingCount -ge 0) {
            $env:MELONDS_NSML_FORCE_WIFI_COMMUNICATING_COUNT = "$ForceWifiCommunicatingCount"
            $env:MELONDS_NSML_FORCE_WIFI_COMMUNICATING_START_FRAME = "$ForceWifiCommunicatingStartFrame"
            $env:MELONDS_NSML_FORCE_WIFI_COMMUNICATING_END_FRAME = "$ForceWifiCommunicatingEndFrame"
        } else {
            Remove-Item Env:\MELONDS_NSML_FORCE_WIFI_COMMUNICATING_COUNT -ErrorAction SilentlyContinue
            Remove-Item Env:\MELONDS_NSML_FORCE_WIFI_COMMUNICATING_START_FRAME -ErrorAction SilentlyContinue
            Remove-Item Env:\MELONDS_NSML_FORCE_WIFI_COMMUNICATING_END_FRAME -ErrorAction SilentlyContinue
        }
        if ($PacketBridgeArmOnly) {
            $env:MELONDS_NSML_PACKET_BRIDGE_ARM_ONLY = "1"
            if ($PacketBridgeTrace) {
                $env:MELONDS_NSML_PACKET_BRIDGE_TRACE = "1"
                $env:MELONDS_NSML_PACKET_REPLAY_LOG = "$Stdout.packet-replay.csv"
            }
        } else { Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_ARM_ONLY -ErrorAction SilentlyContinue }
        if ($ScriptRemotePacket) {
            $env:MELONDS_NSML_SCRIPT_REMOTE_PACKET = "1"
            $env:MELONDS_NSML_SCRIPT_REMOTE_PACKET_PLAYER = "$ScriptRemotePacketPlayer"
            $env:MELONDS_NSML_SCRIPT_REMOTE_PACKET_INPUT_INSTANCE = "$ScriptRemotePacketInputInstance"
            if ($ScriptRemotePacketInputScript) {
                $env:MELONDS_NSML_SCRIPT_REMOTE_PACKET_INPUT_SCRIPT = (Resolve-Path $ScriptRemotePacketInputScript).Path
            } elseif ($Role -eq "host") {
                $env:MELONDS_NSML_SCRIPT_REMOTE_PACKET_INPUT_SCRIPT = (Resolve-Path $clientInput).Path
            } else {
                $env:MELONDS_NSML_SCRIPT_REMOTE_PACKET_INPUT_SCRIPT = (Resolve-Path $hostInput).Path
            }
            $env:MELONDS_NSML_SCRIPT_REMOTE_PACKET_START_FRAME = "$ScriptRemotePacketStartFrame"
            $env:MELONDS_NSML_SCRIPT_REMOTE_PACKET_END_FRAME = "$ScriptRemotePacketEndFrame"
        } else {
            Remove-Item Env:\MELONDS_NSML_SCRIPT_REMOTE_PACKET -ErrorAction SilentlyContinue
            Remove-Item Env:\MELONDS_NSML_SCRIPT_REMOTE_PACKET_PLAYER -ErrorAction SilentlyContinue
            Remove-Item Env:\MELONDS_NSML_SCRIPT_REMOTE_PACKET_INPUT_INSTANCE -ErrorAction SilentlyContinue
            Remove-Item Env:\MELONDS_NSML_SCRIPT_REMOTE_PACKET_INPUT_SCRIPT -ErrorAction SilentlyContinue
            Remove-Item Env:\MELONDS_NSML_SCRIPT_REMOTE_PACKET_START_FRAME -ErrorAction SilentlyContinue
            Remove-Item Env:\MELONDS_NSML_SCRIPT_REMOTE_PACKET_END_FRAME -ErrorAction SilentlyContinue
        }
        Remove-Item Env:\MELONDS_NSML_FORCE_STAGE_SCENE_START_GATE_START_FRAME -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_FORCE_STAGE_SCENE_START_GATE_END_FRAME -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_FORCE_STAGE_SCENE_START_GATE_VALUE -ErrorAction SilentlyContinue
    }
    if ($ForceStageSceneContinueGate) {
        $env:MELONDS_NSML_FORCE_STAGE_SCENE_CONTINUE_GATE = "1"
        if ($ForceStageSceneContinueGateHostOnly) { $env:MELONDS_NSML_FORCE_STAGE_SCENE_CONTINUE_GATE_HOST_ONLY = "1" } else { Remove-Item Env:\MELONDS_NSML_FORCE_STAGE_SCENE_CONTINUE_GATE_HOST_ONLY -ErrorAction SilentlyContinue }
        if ($ForceStageSceneContinueGateClientOnly) { $env:MELONDS_NSML_FORCE_STAGE_SCENE_CONTINUE_GATE_CLIENT_ONLY = "1" } else { Remove-Item Env:\MELONDS_NSML_FORCE_STAGE_SCENE_CONTINUE_GATE_CLIENT_ONLY -ErrorAction SilentlyContinue }
        $env:MELONDS_NSML_FORCE_STAGE_SCENE_CONTINUE_GATE_START_FRAME = "$ForceStageSceneContinueGateStartFrame"
        $env:MELONDS_NSML_FORCE_STAGE_SCENE_CONTINUE_GATE_END_FRAME = "$ForceStageSceneContinueGateEndFrame"
        $env:MELONDS_NSML_FORCE_STAGE_SCENE_CONTINUE_GATE_VALUE = "$ForceStageSceneContinueGateValue"
    } else {
        Remove-Item Env:\MELONDS_NSML_FORCE_STAGE_SCENE_CONTINUE_GATE -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_FORCE_STAGE_SCENE_CONTINUE_GATE_HOST_ONLY -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_FORCE_STAGE_SCENE_CONTINUE_GATE_CLIENT_ONLY -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_FORCE_STAGE_SCENE_CONTINUE_GATE_START_FRAME -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_FORCE_STAGE_SCENE_CONTINUE_GATE_END_FRAME -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_FORCE_STAGE_SCENE_CONTINUE_GATE_VALUE -ErrorAction SilentlyContinue
    }
    if ($ForceStageSceneState3Gate) {
        $env:MELONDS_NSML_FORCE_STAGE_SCENE_STATE3_GATE = "1"
        if ($ForceStageSceneState3GateHostOnly) { $env:MELONDS_NSML_FORCE_STAGE_SCENE_STATE3_GATE_HOST_ONLY = "1" } else { Remove-Item Env:\MELONDS_NSML_FORCE_STAGE_SCENE_STATE3_GATE_HOST_ONLY -ErrorAction SilentlyContinue }
        if ($ForceStageSceneState3GateClientOnly) { $env:MELONDS_NSML_FORCE_STAGE_SCENE_STATE3_GATE_CLIENT_ONLY = "1" } else { Remove-Item Env:\MELONDS_NSML_FORCE_STAGE_SCENE_STATE3_GATE_CLIENT_ONLY -ErrorAction SilentlyContinue }
        $env:MELONDS_NSML_FORCE_STAGE_SCENE_STATE3_GATE_START_FRAME = "$ForceStageSceneState3GateStartFrame"
        $env:MELONDS_NSML_FORCE_STAGE_SCENE_STATE3_GATE_END_FRAME = "$ForceStageSceneState3GateEndFrame"
        $env:MELONDS_NSML_FORCE_STAGE_SCENE_STATE3_GATE_VALUE = "$ForceStageSceneState3GateValue"
    } else {
        Remove-Item Env:\MELONDS_NSML_FORCE_STAGE_SCENE_STATE3_GATE -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_FORCE_STAGE_SCENE_STATE3_GATE_HOST_ONLY -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_FORCE_STAGE_SCENE_STATE3_GATE_CLIENT_ONLY -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_FORCE_STAGE_SCENE_STATE3_GATE_START_FRAME -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_FORCE_STAGE_SCENE_STATE3_GATE_END_FRAME -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_FORCE_STAGE_SCENE_STATE3_GATE_VALUE -ErrorAction SilentlyContinue
    }
    if ($ForceStageSceneEventFlags) {
        $env:MELONDS_NSML_FORCE_STAGE_SCENE_EVENT_FLAGS = "1"
        if ($ForceStageSceneEventFlagsHostOnly) { $env:MELONDS_NSML_FORCE_STAGE_SCENE_EVENT_FLAGS_HOST_ONLY = "1" } else { Remove-Item Env:\MELONDS_NSML_FORCE_STAGE_SCENE_EVENT_FLAGS_HOST_ONLY -ErrorAction SilentlyContinue }
        if ($ForceStageSceneEventFlagsClientOnly) { $env:MELONDS_NSML_FORCE_STAGE_SCENE_EVENT_FLAGS_CLIENT_ONLY = "1" } else { Remove-Item Env:\MELONDS_NSML_FORCE_STAGE_SCENE_EVENT_FLAGS_CLIENT_ONLY -ErrorAction SilentlyContinue }
        $env:MELONDS_NSML_FORCE_STAGE_SCENE_EVENT_FLAGS_START_FRAME = "$ForceStageSceneEventFlagsStartFrame"
        $env:MELONDS_NSML_FORCE_STAGE_SCENE_EVENT_FLAGS_END_FRAME = "$ForceStageSceneEventFlagsEndFrame"
        $env:MELONDS_NSML_FORCE_STAGE_SCENE_EVENT_FLAGS_VALUE = "$ForceStageSceneEventFlagsValue"
    } else {
        Remove-Item Env:\MELONDS_NSML_FORCE_STAGE_SCENE_EVENT_FLAGS -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_FORCE_STAGE_SCENE_EVENT_FLAGS_HOST_ONLY -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_FORCE_STAGE_SCENE_EVENT_FLAGS_CLIENT_ONLY -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_FORCE_STAGE_SCENE_EVENT_FLAGS_START_FRAME -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_FORCE_STAGE_SCENE_EVENT_FLAGS_END_FRAME -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_FORCE_STAGE_SCENE_EVENT_FLAGS_VALUE -ErrorAction SilentlyContinue
    }
    if ($ForceMvlPlayerReady) {
        $env:MELONDS_NSML_FORCE_MVL_PLAYER_READY = "1"
        if ($ForceMvlPlayerReadyHostOnly) { $env:MELONDS_NSML_FORCE_MVL_PLAYER_READY_HOST_ONLY = "1" } else { Remove-Item Env:\MELONDS_NSML_FORCE_MVL_PLAYER_READY_HOST_ONLY -ErrorAction SilentlyContinue }
        if ($ForceMvlPlayerReadyClientOnly) { $env:MELONDS_NSML_FORCE_MVL_PLAYER_READY_CLIENT_ONLY = "1" } else { Remove-Item Env:\MELONDS_NSML_FORCE_MVL_PLAYER_READY_CLIENT_ONLY -ErrorAction SilentlyContinue }
        $env:MELONDS_NSML_FORCE_MVL_PLAYER_READY_START_FRAME = "$ForceMvlPlayerReadyStartFrame"
        $env:MELONDS_NSML_FORCE_MVL_PLAYER_READY_END_FRAME = "$ForceMvlPlayerReadyEndFrame"
        $env:MELONDS_NSML_FORCE_MVL_PLAYER_READY_VALUE = "$ForceMvlPlayerReadyValue"
        if ($ForceMvlPlayerReadySetA8EC) { $env:MELONDS_NSML_FORCE_MVL_PLAYER_READY_SET_A8EC = "1" } else { Remove-Item Env:\MELONDS_NSML_FORCE_MVL_PLAYER_READY_SET_A8EC -ErrorAction SilentlyContinue }
        $env:MELONDS_NSML_FORCE_MVL_PLAYER_READY_A8EC_VALUE = "$ForceMvlPlayerReadyA8ECValue"
    } else {
        Remove-Item Env:\MELONDS_NSML_FORCE_MVL_PLAYER_READY -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_FORCE_MVL_PLAYER_READY_HOST_ONLY -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_FORCE_MVL_PLAYER_READY_CLIENT_ONLY -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_FORCE_MVL_PLAYER_READY_START_FRAME -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_FORCE_MVL_PLAYER_READY_END_FRAME -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_FORCE_MVL_PLAYER_READY_VALUE -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_FORCE_MVL_PLAYER_READY_SET_A8EC -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_FORCE_MVL_PLAYER_READY_A8EC_VALUE -ErrorAction SilentlyContinue
    }
    if ($ForceMvlRuntimeState) {
        $env:MELONDS_NSML_FORCE_MVL_RUNTIME_STATE = "1"
        if ($ForceMvlRuntimeStateHostOnly) { $env:MELONDS_NSML_FORCE_MVL_RUNTIME_STATE_HOST_ONLY = "1" } else { Remove-Item Env:\MELONDS_NSML_FORCE_MVL_RUNTIME_STATE_HOST_ONLY -ErrorAction SilentlyContinue }
        if ($ForceMvlRuntimeStateClientOnly) { $env:MELONDS_NSML_FORCE_MVL_RUNTIME_STATE_CLIENT_ONLY = "1" } else { Remove-Item Env:\MELONDS_NSML_FORCE_MVL_RUNTIME_STATE_CLIENT_ONLY -ErrorAction SilentlyContinue }
        $env:MELONDS_NSML_FORCE_MVL_RUNTIME_STATE_START_FRAME = "$ForceMvlRuntimeStateStartFrame"
        $env:MELONDS_NSML_FORCE_MVL_RUNTIME_STATE_END_FRAME = "$ForceMvlRuntimeStateEndFrame"
        $env:MELONDS_NSML_FORCE_MVL_RUNTIME_STATE_VALUE = "$ForceMvlRuntimeStateValue"
    } else {
        Remove-Item Env:\MELONDS_NSML_FORCE_MVL_RUNTIME_STATE -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_FORCE_MVL_RUNTIME_STATE_HOST_ONLY -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_FORCE_MVL_RUNTIME_STATE_CLIENT_ONLY -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_FORCE_MVL_RUNTIME_STATE_START_FRAME -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_FORCE_MVL_RUNTIME_STATE_END_FRAME -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_FORCE_MVL_RUNTIME_STATE_VALUE -ErrorAction SilentlyContinue
    }
    if ($PacketReplayFile) {
        $env:MELONDS_NSML_PACKET_REPLAY_FILE = (Resolve-Path $PacketReplayFile).Path
        $env:MELONDS_NSML_PACKET_REPLAY_LOG = "$Stdout.packet-replay.csv"
    } else {
        Remove-Item Env:\MELONDS_NSML_PACKET_REPLAY_FILE -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_PACKET_REPLAY_LOG -ErrorAction SilentlyContinue
    }
    if ($PacketBridgeArmOnly -and $PacketBridgeTrace) {
        $env:MELONDS_NSML_PACKET_REPLAY_LOG = "$Stdout.packet-replay.csv"
    }
    if ($PacketCapture) {
        $env:MELONDS_NSML_PACKET_CAPTURE_LOG = $PacketCapturePath
        if ($PacketCaptureAllowPreGame) {
            $env:MELONDS_NSML_PACKET_BRIDGE_ALLOW_PRE_GAME = "1"
        }
    } else {
        Remove-Item Env:\MELONDS_NSML_PACKET_CAPTURE_LOG -ErrorAction SilentlyContinue
        if (-not $PacketBridge) {
            Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_ALLOW_PRE_GAME -ErrorAction SilentlyContinue
        }
    }
    if ($PacketBridge) {
        $env:MELONDS_NSML_POC = "1"
        $env:MELONDS_NSML_ROLE = $Role
        $env:MELONDS_NSML_PORT = "$PacketBridgePort"
        if ($Role -eq "host" -and $HostLocalInstance) {
            $env:MELONDS_NSML_LOCAL_INSTANCE = $HostLocalInstance
        } elseif ($Role -eq "client" -and $ClientLocalInstance) {
            $env:MELONDS_NSML_LOCAL_INSTANCE = $ClientLocalInstance
        } elseif ($PacketBridgeAllowPreGame -and $Role -eq "client") {
            $env:MELONDS_NSML_LOCAL_INSTANCE = "1"
        } else {
            $env:MELONDS_NSML_LOCAL_INSTANCE = "0"
        }
        $env:MELONDS_NSML_PACKET_BRIDGE = "1"
        $env:MELONDS_NSML_PACKET_BRIDGE_ONLY = "1"
        if ($PacketBridgeAllowJit) {
            $env:MELONDS_NSML_PACKET_BRIDGE_ALLOW_JIT = "1"
        } else {
            Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_ALLOW_JIT -ErrorAction SilentlyContinue
        }
        if ($Role -eq "host" -and $HostPacketBridgeLocalPlayer) {
            $env:MELONDS_NSML_PACKET_BRIDGE_LOCAL_PLAYER = $HostPacketBridgeLocalPlayer
        } elseif ($Role -eq "client" -and $ClientPacketBridgeLocalPlayer) {
            $env:MELONDS_NSML_PACKET_BRIDGE_LOCAL_PLAYER = $ClientPacketBridgeLocalPlayer
        } else {
            Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_LOCAL_PLAYER -ErrorAction SilentlyContinue
        }
        if ($Role -eq "host" -and $HostPacketBridgeLoadLevelPlayerID) {
            $env:MELONDS_NSML_PACKET_BRIDGE_LOAD_LEVEL_PLAYER_ID = $HostPacketBridgeLoadLevelPlayerID
        } elseif ($Role -eq "client" -and $ClientPacketBridgeLoadLevelPlayerID) {
            $env:MELONDS_NSML_PACKET_BRIDGE_LOAD_LEVEL_PLAYER_ID = $ClientPacketBridgeLoadLevelPlayerID
        } else {
            Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_LOAD_LEVEL_PLAYER_ID -ErrorAction SilentlyContinue
        }
        if ($Role -eq "host" -and $HostPacketBridgeForceGameLocalPlayerID) {
            $env:MELONDS_NSML_PACKET_BRIDGE_FORCE_GAME_LOCAL_PLAYER_ID = $HostPacketBridgeForceGameLocalPlayerID
            $env:MELONDS_NSML_PACKET_BRIDGE_FORCE_GAME_LOCAL_PLAYER_ID_START_FRAME = "$PacketBridgeForceGameLocalPlayerIDStartFrame"
        } elseif ($Role -eq "client" -and $ClientPacketBridgeForceGameLocalPlayerID) {
            $env:MELONDS_NSML_PACKET_BRIDGE_FORCE_GAME_LOCAL_PLAYER_ID = $ClientPacketBridgeForceGameLocalPlayerID
            $env:MELONDS_NSML_PACKET_BRIDGE_FORCE_GAME_LOCAL_PLAYER_ID_START_FRAME = "$PacketBridgeForceGameLocalPlayerIDStartFrame"
        } else {
            Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_FORCE_GAME_LOCAL_PLAYER_ID -ErrorAction SilentlyContinue
            Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_FORCE_GAME_LOCAL_PLAYER_ID_START_FRAME -ErrorAction SilentlyContinue
        }
        if ($PacketBridgeForceGameLocalPlayerIDEarly) {
            $env:MELONDS_NSML_PACKET_BRIDGE_FORCE_GAME_LOCAL_PLAYER_ID_EARLY = "1"
        } else {
            Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_FORCE_GAME_LOCAL_PLAYER_ID_EARLY -ErrorAction SilentlyContinue
        }
        if ($PacketBridgeAllowPreGame) {
            $env:MELONDS_NSML_PACKET_BRIDGE_ALLOW_PRE_GAME = "1"
        } else {
            Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_ALLOW_PRE_GAME -ErrorAction SilentlyContinue
        }
        $roleReplayOffset = $PacketBridgeReplayTickOffset
        if ($Role -eq "host" -and $HostPacketBridgeReplayTickOffset -ne [int]::MinValue) {
            $roleReplayOffset = $HostPacketBridgeReplayTickOffset
        } elseif ($Role -eq "client" -and $ClientPacketBridgeReplayTickOffset -ne [int]::MinValue) {
            $roleReplayOffset = $ClientPacketBridgeReplayTickOffset
        }
        $env:MELONDS_NSML_PACKET_BRIDGE_REPLAY_TICK_OFFSET = "$roleReplayOffset"
        if ($PacketBridgeWait) {
            $env:MELONDS_NSML_PACKET_BRIDGE_WAIT = "1"
            $env:MELONDS_NSML_PACKET_BRIDGE_WAIT_TIMEOUT_MS = "$PacketBridgeWaitTimeoutMs"
            $env:MELONDS_NSML_PACKET_BRIDGE_WAIT_START_FRAME = "$PacketBridgeWaitStartFrame"
            $env:MELONDS_NSML_PACKET_BRIDGE_WAIT_TICK_AHEAD = "$PacketBridgeWaitTickAhead"
        } else {
            Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_WAIT -ErrorAction SilentlyContinue
            Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_WAIT_TIMEOUT_MS -ErrorAction SilentlyContinue
            Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_WAIT_START_FRAME -ErrorAction SilentlyContinue
            Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_WAIT_TICK_AHEAD -ErrorAction SilentlyContinue
        }
        if ($PacketBridgeDirectCapture) {
            $env:MELONDS_NSML_PACKET_BRIDGE_DIRECT_CAPTURE = "1"
        } else {
            Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_DIRECT_CAPTURE -ErrorAction SilentlyContinue
        }
        if ($PacketBridgeFakePeerInfo) {
            $env:MELONDS_NSML_PACKET_BRIDGE_FAKE_PEER_INFO = "1"
        } else {
            Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_FAKE_PEER_INFO -ErrorAction SilentlyContinue
        }
        if ($PacketBridgeBypassStartConnection) {
            $env:MELONDS_NSML_PACKET_BRIDGE_BYPASS_START_CONNECTION = "1"
            $env:MELONDS_NSML_PACKET_BRIDGE_BYPASS_START_CONNECTION_START_FRAME = "$PacketBridgeBypassStartConnectionStartFrame"
            if ($PacketBridgeBypassStartConnectionClientOnly) {
                $env:MELONDS_NSML_PACKET_BRIDGE_BYPASS_START_CONNECTION_CLIENT_ONLY = "1"
            } else {
                Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_BYPASS_START_CONNECTION_CLIENT_ONLY -ErrorAction SilentlyContinue
            }
        } else {
            Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_BYPASS_START_CONNECTION -ErrorAction SilentlyContinue
            Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_BYPASS_START_CONNECTION_START_FRAME -ErrorAction SilentlyContinue
            Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_BYPASS_START_CONNECTION_CLIENT_ONLY -ErrorAction SilentlyContinue
        }
        if ($PacketBridgeBypassWifiStart) {
            $env:MELONDS_NSML_PACKET_BRIDGE_BYPASS_WIFI_START = "1"
            $env:MELONDS_NSML_PACKET_BRIDGE_BYPASS_WIFI_START_START_FRAME = "$PacketBridgeBypassWifiStartFrame"
        } else {
            Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_BYPASS_WIFI_START -ErrorAction SilentlyContinue
            Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_BYPASS_WIFI_START_START_FRAME -ErrorAction SilentlyContinue
        }
        if ($PacketBridgeLowerStatusResult -ge 0) {
            $env:MELONDS_NSML_PACKET_BRIDGE_LOWER_STATUS_RESULT = "$PacketBridgeLowerStatusResult"
        } else {
            Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_LOWER_STATUS_RESULT -ErrorAction SilentlyContinue
        }
        if ($PacketBridgeForceTick) {
            $env:MELONDS_NSML_PACKET_BRIDGE_FORCE_TICK = "1"
            $env:MELONDS_NSML_PACKET_BRIDGE_FORCE_TICK_START_FRAME = "$PacketBridgeForceTickStartFrame"
            $roleForceTickBase = $PacketBridgeForceTickBase
            if ($Role -eq "host" -and $HostPacketBridgeForceTickBase -ge 0) {
                $roleForceTickBase = $HostPacketBridgeForceTickBase
            } elseif ($Role -eq "client" -and $ClientPacketBridgeForceTickBase -ge 0) {
                $roleForceTickBase = $ClientPacketBridgeForceTickBase
            }
            if ($roleForceTickBase -ge 0) {
                $env:MELONDS_NSML_PACKET_BRIDGE_FORCE_TICK_BASE = "$roleForceTickBase"
            } else {
                Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_FORCE_TICK_BASE -ErrorAction SilentlyContinue
            }
        } else {
            Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_FORCE_TICK -ErrorAction SilentlyContinue
            Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_FORCE_TICK_START_FRAME -ErrorAction SilentlyContinue
            Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_FORCE_TICK_BASE -ErrorAction SilentlyContinue
        }
        if ($PacketBridgeForceNetReady) {
            $env:MELONDS_NSML_PACKET_BRIDGE_FORCE_NET_READY = "1"
            $env:MELONDS_NSML_PACKET_BRIDGE_FORCE_NET_READY_START_FRAME = "$PacketBridgeForceNetReadyStartFrame"
            if ($PacketBridgeForceNetReadyEndFrame -gt 0) { $env:MELONDS_NSML_PACKET_BRIDGE_FORCE_NET_READY_END_FRAME = "$PacketBridgeForceNetReadyEndFrame" } else { Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_FORCE_NET_READY_END_FRAME -ErrorAction SilentlyContinue }
            if ($PacketBridgeForceNetReadyHostOnly) { $env:MELONDS_NSML_PACKET_BRIDGE_FORCE_NET_READY_HOST_ONLY = "1" } else { Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_FORCE_NET_READY_HOST_ONLY -ErrorAction SilentlyContinue }
            if ($PacketBridgeForceNetReadyClientOnly) { $env:MELONDS_NSML_PACKET_BRIDGE_FORCE_NET_READY_CLIENT_ONLY = "1" } else { Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_FORCE_NET_READY_CLIENT_ONLY -ErrorAction SilentlyContinue }
            if ($PacketBridgeForceStageSceneArg) { $env:MELONDS_NSML_PACKET_BRIDGE_FORCE_STAGE_SCENE_ARG = "1" } else { Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_FORCE_STAGE_SCENE_ARG -ErrorAction SilentlyContinue }
            if ($PacketBridgeForceNetReadyState10) { $env:MELONDS_NSML_PACKET_BRIDGE_FORCE_NET_READY_STATE10 = "1" } else { Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_FORCE_NET_READY_STATE10 -ErrorAction SilentlyContinue }
            if ($PacketBridgeForceNetReadyState10ClientOnly) { $env:MELONDS_NSML_PACKET_BRIDGE_FORCE_NET_READY_STATE10_CLIENT_ONLY = "1" } else { Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_FORCE_NET_READY_STATE10_CLIENT_ONLY -ErrorAction SilentlyContinue }
            if ($PacketBridgeForceCourseSelectReady) { $env:MELONDS_NSML_PACKET_BRIDGE_FORCE_COURSE_SELECT_READY = "1" } else { Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_FORCE_COURSE_SELECT_READY -ErrorAction SilentlyContinue }
        } else {
            Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_FORCE_NET_READY -ErrorAction SilentlyContinue
            Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_FORCE_NET_READY_START_FRAME -ErrorAction SilentlyContinue
            Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_FORCE_NET_READY_END_FRAME -ErrorAction SilentlyContinue
            Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_FORCE_NET_READY_HOST_ONLY -ErrorAction SilentlyContinue
            Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_FORCE_NET_READY_CLIENT_ONLY -ErrorAction SilentlyContinue
            Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_FORCE_STAGE_SCENE_ARG -ErrorAction SilentlyContinue
            Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_FORCE_NET_READY_STATE10 -ErrorAction SilentlyContinue
            Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_FORCE_NET_READY_STATE10_CLIENT_ONLY -ErrorAction SilentlyContinue
            Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_FORCE_COURSE_SELECT_READY -ErrorAction SilentlyContinue
        }
        if ($PacketBridgeForceCourseSelectReady) {
            $env:MELONDS_NSML_PACKET_BRIDGE_FORCE_COURSE_SELECT_READY = "1"
            $env:MELONDS_NSML_PACKET_BRIDGE_FORCE_COURSE_SELECT_READY_START_FRAME = "$PacketBridgeForceCourseSelectReadyStartFrame"
        } else {
            Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_FORCE_COURSE_SELECT_READY -ErrorAction SilentlyContinue
            Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_FORCE_COURSE_SELECT_READY_START_FRAME -ErrorAction SilentlyContinue
        }
        if ($PacketBridgeForceLoadGameSM) {
            $env:MELONDS_NSML_PACKET_BRIDGE_FORCE_LOAD_GAME_SM = "1"
            $env:MELONDS_NSML_PACKET_BRIDGE_FORCE_LOAD_GAME_SM_START_FRAME = "$PacketBridgeForceLoadGameSMStartFrame"
            $env:MELONDS_NSML_PACKET_BRIDGE_FORCE_LOAD_GAME_SM_STEP = "$PacketBridgeForceLoadGameSMStep"
            if ($PacketBridgeForceLoadGameSMTimer -ge 0) {
                $env:MELONDS_NSML_PACKET_BRIDGE_FORCE_LOAD_GAME_SM_TIMER = "$PacketBridgeForceLoadGameSMTimer"
            } else {
                Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_FORCE_LOAD_GAME_SM_TIMER -ErrorAction SilentlyContinue
            }
            if ($PacketBridgeForceLoadGameSMFlags -ge 0) {
                $env:MELONDS_NSML_PACKET_BRIDGE_FORCE_LOAD_GAME_SM_FLAGS = "$PacketBridgeForceLoadGameSMFlags"
            } else {
                Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_FORCE_LOAD_GAME_SM_FLAGS -ErrorAction SilentlyContinue
            }
            if ($PacketBridgeForceLoadGameSMRunUpdate) {
                $env:MELONDS_NSML_PACKET_BRIDGE_FORCE_LOAD_GAME_SM_RUN_UPDATE = "1"
            } else {
                Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_FORCE_LOAD_GAME_SM_RUN_UPDATE -ErrorAction SilentlyContinue
            }
            if ($PacketBridgeForceLoadGameSMRunUpdateAll) {
                $env:MELONDS_NSML_PACKET_BRIDGE_FORCE_LOAD_GAME_SM_RUN_UPDATE_ALL = "1"
            } else {
                Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_FORCE_LOAD_GAME_SM_RUN_UPDATE_ALL -ErrorAction SilentlyContinue
            }
            if ($PacketBridgeForceLoadGameSMPulseAction) {
                $env:MELONDS_NSML_PACKET_BRIDGE_FORCE_LOAD_GAME_SM_PULSE_ACTION = "1"
            } else {
                Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_FORCE_LOAD_GAME_SM_PULSE_ACTION -ErrorAction SilentlyContinue
            }
            if ($PacketBridgeForceLoadGameSMBaselineFlags) {
                $env:MELONDS_NSML_PACKET_BRIDGE_FORCE_LOAD_GAME_SM_BASELINE_FLAGS = "1"
            } else {
                Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_FORCE_LOAD_GAME_SM_BASELINE_FLAGS -ErrorAction SilentlyContinue
            }
            if ($PacketBridgeForceLoadGameSMPreload) {
                $env:MELONDS_NSML_PACKET_BRIDGE_FORCE_LOAD_GAME_SM_PRELOAD = "1"
            } else {
                Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_FORCE_LOAD_GAME_SM_PRELOAD -ErrorAction SilentlyContinue
            }
            if ($PacketBridgeForceLoadGameSMAllowCourseSelect) {
                $env:MELONDS_NSML_PACKET_BRIDGE_FORCE_LOAD_GAME_SM_ALLOW_COURSE_SELECT = "1"
            } else {
                Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_FORCE_LOAD_GAME_SM_ALLOW_COURSE_SELECT -ErrorAction SilentlyContinue
            }
            $forceStagePacketWordsForRole = $PacketBridgeForceStagePacketWords
            if ($PacketBridgeForceStagePacketWordsHostOnly -and $Role -ne "host") {
                $forceStagePacketWordsForRole = $false
            }
            if ($PacketBridgeForceStagePacketWordsClientOnly -and $Role -ne "client") {
                $forceStagePacketWordsForRole = $false
            }
            if ($forceStagePacketWordsForRole) {
                $env:MELONDS_NSML_PACKET_BRIDGE_FORCE_STAGE_PACKET_WORDS = "1"
                $env:MELONDS_NSML_PACKET_BRIDGE_FORCE_STAGE_PACKET_WORDS_START_FRAME = "$PacketBridgeForceStagePacketWordsStartFrame"
                if ($PacketBridgeForceStagePacketWordsEndFrame -gt 0) {
                    $env:MELONDS_NSML_PACKET_BRIDGE_FORCE_STAGE_PACKET_WORDS_END_FRAME = "$PacketBridgeForceStagePacketWordsEndFrame"
                } else {
                    Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_FORCE_STAGE_PACKET_WORDS_END_FRAME -ErrorAction SilentlyContinue
                }
                if ($PacketBridgeForceStageNet20OnStageScene) {
                    $env:MELONDS_NSML_PACKET_BRIDGE_FORCE_STAGE_NET20_ON_STAGE_SCENE = "1"
                } else {
                    Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_FORCE_STAGE_NET20_ON_STAGE_SCENE -ErrorAction SilentlyContinue
                }
            } else {
                Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_FORCE_STAGE_PACKET_WORDS -ErrorAction SilentlyContinue
                Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_FORCE_STAGE_PACKET_WORDS_START_FRAME -ErrorAction SilentlyContinue
                Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_FORCE_STAGE_PACKET_WORDS_END_FRAME -ErrorAction SilentlyContinue
                Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_FORCE_STAGE_NET20_ON_STAGE_SCENE -ErrorAction SilentlyContinue
            }
            if ($PacketBridgeScheduleLoadGameSM) {
                $env:MELONDS_NSML_PACKET_BRIDGE_SCHEDULE_LOAD_GAME_SM = "1"
            } else {
                Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_SCHEDULE_LOAD_GAME_SM -ErrorAction SilentlyContinue
            }
            if ($PacketBridgeForceMvlFileCache) {
                $env:MELONDS_NSML_PACKET_BRIDGE_FORCE_MVL_FILE_CACHE = "1"
                $env:MELONDS_NSML_PACKET_BRIDGE_FORCE_MVL_FILE_CACHE_START_FRAME = "$PacketBridgeForceMvlFileCacheStartFrame"
            } else {
                Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_FORCE_MVL_FILE_CACHE -ErrorAction SilentlyContinue
                Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_FORCE_MVL_FILE_CACHE_START_FRAME -ErrorAction SilentlyContinue
            }
            if ($PacketBridgeForceMvlLoadThread) {
                $env:MELONDS_NSML_PACKET_BRIDGE_FORCE_MVL_LOAD_THREAD = "1"
                $env:MELONDS_NSML_PACKET_BRIDGE_FORCE_MVL_LOAD_THREAD_START_FRAME = "$PacketBridgeForceMvlLoadThreadStartFrame"
            } else {
                Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_FORCE_MVL_LOAD_THREAD -ErrorAction SilentlyContinue
                Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_FORCE_MVL_LOAD_THREAD_START_FRAME -ErrorAction SilentlyContinue
            }
        } else {
            Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_FORCE_LOAD_GAME_SM -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_FORCE_LOAD_GAME_SM_START_FRAME -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_FORCE_LOAD_GAME_SM_PULSE_ACTION -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_FORCE_LOAD_GAME_SM_BASELINE_FLAGS -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_FORCE_LOAD_GAME_SM_PRELOAD -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_FORCE_LOAD_GAME_SM_ALLOW_COURSE_SELECT -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_FORCE_STAGE_PACKET_WORDS -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_FORCE_STAGE_PACKET_WORDS_START_FRAME -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_FORCE_STAGE_PACKET_WORDS_END_FRAME -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_FORCE_STAGE_NET20_ON_STAGE_SCENE -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_FORCE_MVL_LOAD_THREAD -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_FORCE_MVL_LOAD_THREAD_START_FRAME -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_SAFE_CREATE_LOAD_GAME_CALL -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_SAFE_CREATE_LOAD_GAME_CALL_FRAME -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_SAFE_CREATE_LOAD_GAME_CALL_PC -ErrorAction SilentlyContinue
        if ($PacketBridgeScheduleLoadGameSM) {
            $env:MELONDS_NSML_PACKET_BRIDGE_SCHEDULE_LOAD_GAME_SM = "1"
        } else {
            Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_SCHEDULE_LOAD_GAME_SM -ErrorAction SilentlyContinue
        }
            Remove-Item Env:\MELONDS_NSML_SAFE_SCHEDULE_LOAD_GAME_CALL -ErrorAction SilentlyContinue
            Remove-Item Env:\MELONDS_NSML_SAFE_SCHEDULE_LOAD_GAME_CALL_FRAME -ErrorAction SilentlyContinue
            Remove-Item Env:\MELONDS_NSML_SAFE_SCHEDULE_LOAD_GAME_CALL_PC -ErrorAction SilentlyContinue
            Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_FORCE_MVL_FILE_CACHE -ErrorAction SilentlyContinue
            Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_FORCE_LOAD_GAME_SM_STEP -ErrorAction SilentlyContinue
            Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_FORCE_LOAD_GAME_SM_TIMER -ErrorAction SilentlyContinue
            Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_FORCE_LOAD_GAME_SM_RUN_UPDATE -ErrorAction SilentlyContinue
            Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_FORCE_LOAD_GAME_SM_RUN_UPDATE_ALL -ErrorAction SilentlyContinue
        }
        $forceStagePacketWordsForRole = $PacketBridgeForceStagePacketWords
        if ($PacketBridgeForceStagePacketWordsHostOnly -and $Role -ne "host") {
            $forceStagePacketWordsForRole = $false
        }
        if ($PacketBridgeForceStagePacketWordsClientOnly -and $Role -ne "client") {
            $forceStagePacketWordsForRole = $false
        }
        if ($forceStagePacketWordsForRole) {
            $env:MELONDS_NSML_PACKET_BRIDGE_FORCE_STAGE_PACKET_WORDS = "1"
            $env:MELONDS_NSML_PACKET_BRIDGE_FORCE_STAGE_PACKET_WORDS_START_FRAME = "$PacketBridgeForceStagePacketWordsStartFrame"
            if ($PacketBridgeForceStagePacketWordsEndFrame -gt 0) {
                $env:MELONDS_NSML_PACKET_BRIDGE_FORCE_STAGE_PACKET_WORDS_END_FRAME = "$PacketBridgeForceStagePacketWordsEndFrame"
            } else {
                Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_FORCE_STAGE_PACKET_WORDS_END_FRAME -ErrorAction SilentlyContinue
            }
            if ($PacketBridgeForceStageNet20OnStageScene) {
                $env:MELONDS_NSML_PACKET_BRIDGE_FORCE_STAGE_NET20_ON_STAGE_SCENE = "1"
            } else {
                Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_FORCE_STAGE_NET20_ON_STAGE_SCENE -ErrorAction SilentlyContinue
            }
        } else {
            Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_FORCE_STAGE_PACKET_WORDS -ErrorAction SilentlyContinue
            Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_FORCE_STAGE_PACKET_WORDS_START_FRAME -ErrorAction SilentlyContinue
            Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_FORCE_STAGE_PACKET_WORDS_END_FRAME -ErrorAction SilentlyContinue
            Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_FORCE_STAGE_NET20_ON_STAGE_SCENE -ErrorAction SilentlyContinue
        }
        if ($PacketBridgeDummyAlloc) {
            $env:MELONDS_NSML_PACKET_BRIDGE_DUMMY_ALLOC = "1"
            $env:MELONDS_NSML_PACKET_BRIDGE_DUMMY_ALLOC_FRAME = "$PacketBridgeDummyAllocFrame"
            $env:MELONDS_NSML_PACKET_BRIDGE_DUMMY_ALLOC_SIZE = "$PacketBridgeDummyAllocSize"
        } else {
            Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_DUMMY_ALLOC -ErrorAction SilentlyContinue
            Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_DUMMY_ALLOC_FRAME -ErrorAction SilentlyContinue
            Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_DUMMY_ALLOC_SIZE -ErrorAction SilentlyContinue
        }
        if ($PacketBridgeLookupTickDelay -gt 0) {
            $env:MELONDS_NSML_PACKET_REPLAY_LOOKUP_TICK_DELAY = "$PacketBridgeLookupTickDelay"
        } else {
            Remove-Item Env:\MELONDS_NSML_PACKET_REPLAY_LOOKUP_TICK_DELAY -ErrorAction SilentlyContinue
        }
        $effectivePacketBridgeLocalInputDelay = $PacketBridgeLocalInputDelay
        if ($effectivePacketBridgeLocalInputDelay -lt 0) {
            $effectivePacketBridgeLocalInputDelay = $PacketBridgeLookupTickDelay
        }
        if ($effectivePacketBridgeLocalInputDelay -gt 0) {
            $env:MELONDS_NSML_PACKET_BRIDGE_LOCAL_INPUT_DELAY = "$effectivePacketBridgeLocalInputDelay"
        } else {
            Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_LOCAL_INPUT_DELAY -ErrorAction SilentlyContinue
        }
        if ($PacketBridgeNeutralizeLocalInput) {
            $env:MELONDS_NSML_PACKET_BRIDGE_NEUTRALIZE_LOCAL_INPUT = "1"
        } else {
            Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_NEUTRALIZE_LOCAL_INPUT -ErrorAction SilentlyContinue
        }
        if ($PacketBridgePreserveLocalTouch) {
            $env:MELONDS_NSML_PACKET_BRIDGE_PRESERVE_LOCAL_TOUCH = "1"
        } else {
            Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_PRESERVE_LOCAL_TOUCH -ErrorAction SilentlyContinue
        }
        if ($PacketBridgeSendDelayFrames -gt 0) {
            $env:MELONDS_NSML_PACKET_BRIDGE_SEND_DELAY_FRAMES = "$PacketBridgeSendDelayFrames"
        } else {
            Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_SEND_DELAY_FRAMES -ErrorAction SilentlyContinue
        }
        if ($PacketBridgeSendJitterFrames -gt 0) {
            $env:MELONDS_NSML_PACKET_BRIDGE_SEND_JITTER_FRAMES = "$PacketBridgeSendJitterFrames"
        } else {
            Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_SEND_JITTER_FRAMES -ErrorAction SilentlyContinue
        }
        if ($PacketBridgeMaxPumpEvents -gt 0) {
            $env:MELONDS_NSML_PACKET_BRIDGE_MAX_PUMP_EVENTS = "$PacketBridgeMaxPumpEvents"
        } else {
            Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_MAX_PUMP_EVENTS -ErrorAction SilentlyContinue
        }
        if ($PacketBridgeSuppressDisconnect) {
            $env:MELONDS_NSML_PACKET_BRIDGE_SUPPRESS_DISCONNECT = "1"
        } else {
            Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_SUPPRESS_DISCONNECT -ErrorAction SilentlyContinue
        }
        if ($PacketBridgeSuppressBlackout) {
            $env:MELONDS_NSML_PACKET_BRIDGE_SUPPRESS_BLACKOUT = "1"
        } else {
            Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_SUPPRESS_BLACKOUT -ErrorAction SilentlyContinue
        }
        if ($PacketBridgePreserveNetPointers) {
            $env:MELONDS_NSML_PACKET_BRIDGE_PRESERVE_NET_POINTERS = "1"
        } else {
            Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_PRESERVE_NET_POINTERS -ErrorAction SilentlyContinue
        }
        if ($PacketBridgeBypassNetReset) {
            $env:MELONDS_NSML_PACKET_BRIDGE_BYPASS_NET_RESET = "1"
        } else {
            Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_BYPASS_NET_RESET -ErrorAction SilentlyContinue
        }
        if ($PacketBridgeBypassNetDisconnect) {
            $env:MELONDS_NSML_PACKET_BRIDGE_BYPASS_NET_DISCONNECT = "1"
            $env:MELONDS_NSML_PACKET_BRIDGE_BYPASS_NET_DISCONNECT_MODE = $PacketBridgeBypassNetDisconnectMode
            if ($PacketBridgeBypassNetDisconnectStartFrame -gt 0) {
                $env:MELONDS_NSML_PACKET_BRIDGE_BYPASS_NET_DISCONNECT_START_FRAME = "$PacketBridgeBypassNetDisconnectStartFrame"
            } elseif ($DropMPAfterFrame -gt 0) {
                $env:MELONDS_NSML_PACKET_BRIDGE_BYPASS_NET_DISCONNECT_START_FRAME = "$DropMPAfterFrame"
            } else {
                Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_BYPASS_NET_DISCONNECT_START_FRAME -ErrorAction SilentlyContinue
            }
        } else {
            Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_BYPASS_NET_DISCONNECT -ErrorAction SilentlyContinue
            Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_BYPASS_NET_DISCONNECT_MODE -ErrorAction SilentlyContinue
            Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_BYPASS_NET_DISCONNECT_START_FRAME -ErrorAction SilentlyContinue
        }
        if ($PacketBridgeForceTransferResult) {
            $env:MELONDS_NSML_PACKET_BRIDGE_FORCE_TRANSFER_RESULT = "1"
            if ($PacketBridgeForceTransferClientOnly) { $env:MELONDS_NSML_PACKET_BRIDGE_FORCE_TRANSFER_CLIENT_ONLY = "1" } else { Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_FORCE_TRANSFER_CLIENT_ONLY -ErrorAction SilentlyContinue }
            if ($PacketBridgeForceTransferStartFrame -gt 0) {
                $env:MELONDS_NSML_PACKET_BRIDGE_FORCE_TRANSFER_START_FRAME = "$PacketBridgeForceTransferStartFrame"
            } elseif ($DropMPAfterFrame -gt 0) {
                $env:MELONDS_NSML_PACKET_BRIDGE_FORCE_TRANSFER_START_FRAME = "$DropMPAfterFrame"
            } else {
                Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_FORCE_TRANSFER_START_FRAME -ErrorAction SilentlyContinue
            }
            $env:MELONDS_NSML_PACKET_BRIDGE_FORCE_TRANSFER_RESULT_VALUE = "$PacketBridgeForceTransferResultValue"
        } else {
            Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_FORCE_TRANSFER_RESULT -ErrorAction SilentlyContinue
            Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_FORCE_TRANSFER_CLIENT_ONLY -ErrorAction SilentlyContinue
            Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_FORCE_TRANSFER_START_FRAME -ErrorAction SilentlyContinue
            Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_FORCE_TRANSFER_RESULT_VALUE -ErrorAction SilentlyContinue
        }
        if ($PacketBridgeMaxTickLead -ge 0) {
            $env:MELONDS_NSML_PACKET_BRIDGE_MAX_TICK_LEAD = "$PacketBridgeMaxTickLead"
            $env:MELONDS_NSML_PACKET_BRIDGE_THROTTLE_TIMEOUT_MS = "$PacketBridgeThrottleTimeoutMs"
            $env:MELONDS_NSML_PACKET_BRIDGE_THROTTLE_START_FRAME = "$PacketBridgeThrottleStartFrame"
        } else {
            Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_MAX_TICK_LEAD -ErrorAction SilentlyContinue
        }
        if ($PacketBridgeMaxFrameLead -ge 0) {
            $env:MELONDS_NSML_PACKET_BRIDGE_MAX_FRAME_LEAD = "$PacketBridgeMaxFrameLead"
            $env:MELONDS_NSML_PACKET_BRIDGE_THROTTLE_TIMEOUT_MS = "$PacketBridgeThrottleTimeoutMs"
            $env:MELONDS_NSML_PACKET_BRIDGE_THROTTLE_START_FRAME = "$PacketBridgeThrottleStartFrame"
        } else {
            Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_MAX_FRAME_LEAD -ErrorAction SilentlyContinue
        }
        if ($PacketBridgeMaxTickLead -lt 0 -and $PacketBridgeMaxFrameLead -lt 0) {
            Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_THROTTLE_TIMEOUT_MS -ErrorAction SilentlyContinue
            Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_THROTTLE_START_FRAME -ErrorAction SilentlyContinue
        }
        if ($PacketBridgeStrictRemote -or $PacketBridgeStrictPlayers) {
            $env:MELONDS_NSML_PACKET_REPLAY_STRICT = "1"
            if ($PacketBridgeStrictStartFrame -gt 0) {
                $env:MELONDS_NSML_PACKET_REPLAY_STRICT_START_FRAME = "$PacketBridgeStrictStartFrame"
            } else {
                Remove-Item Env:\MELONDS_NSML_PACKET_REPLAY_STRICT_START_FRAME -ErrorAction SilentlyContinue
            }
            if ($PacketBridgeStrictRequireLead -gt 0) {
                $env:MELONDS_NSML_PACKET_REPLAY_STRICT_REQUIRE_LEAD = "$PacketBridgeStrictRequireLead"
            } else {
                Remove-Item Env:\MELONDS_NSML_PACKET_REPLAY_STRICT_REQUIRE_LEAD -ErrorAction SilentlyContinue
            }
            if ($PacketBridgeStrictPlayers) {
                $env:MELONDS_NSML_PACKET_REPLAY_STRICT_PLAYERS = $PacketBridgeStrictPlayers
            } elseif ($Role -eq "host") {
                $env:MELONDS_NSML_PACKET_REPLAY_STRICT_PLAYERS = "1"
            } else {
                $env:MELONDS_NSML_PACKET_REPLAY_STRICT_PLAYERS = "0"
            }
        } elseif (-not $PacketReplayFile) {
            Remove-Item Env:\MELONDS_NSML_PACKET_REPLAY_STRICT -ErrorAction SilentlyContinue
            Remove-Item Env:\MELONDS_NSML_PACKET_REPLAY_STRICT_PLAYERS -ErrorAction SilentlyContinue
            Remove-Item Env:\MELONDS_NSML_PACKET_REPLAY_STRICT_START_FRAME -ErrorAction SilentlyContinue
            Remove-Item Env:\MELONDS_NSML_PACKET_REPLAY_STRICT_REQUIRE_LEAD -ErrorAction SilentlyContinue
        }
        if ($PacketBridgeLiveFallbackWindow -gt 0) {
            $env:MELONDS_NSML_PACKET_REPLAY_LIVE_FALLBACK_WINDOW = "$PacketBridgeLiveFallbackWindow"
        } else {
            Remove-Item Env:\MELONDS_NSML_PACKET_REPLAY_LIVE_FALLBACK_WINDOW -ErrorAction SilentlyContinue
        }
        if ($PacketBridgeLiveFallbackNearest) {
            $env:MELONDS_NSML_PACKET_REPLAY_LIVE_FALLBACK_NEAREST = "1"
        } else {
            Remove-Item Env:\MELONDS_NSML_PACKET_REPLAY_LIVE_FALLBACK_NEAREST -ErrorAction SilentlyContinue
        }
        if ($PacketBridgeLiveFallbackLatestBefore) {
            $env:MELONDS_NSML_PACKET_REPLAY_LIVE_FALLBACK_LATEST_BEFORE = "1"
        } else {
            Remove-Item Env:\MELONDS_NSML_PACKET_REPLAY_LIVE_FALLBACK_LATEST_BEFORE -ErrorAction SilentlyContinue
        }
        if ($PacketBridgeLiveFallbackStartFrame -gt 0) {
            $env:MELONDS_NSML_PACKET_REPLAY_LIVE_FALLBACK_START_FRAME = "$PacketBridgeLiveFallbackStartFrame"
        } else {
            Remove-Item Env:\MELONDS_NSML_PACKET_REPLAY_LIVE_FALLBACK_START_FRAME -ErrorAction SilentlyContinue
        }
        if ($PacketBridgeReplayReturnLookupTick) {
            $env:MELONDS_NSML_PACKET_REPLAY_RETURN_LOOKUP_TICK = "1"
        } else {
            Remove-Item Env:\MELONDS_NSML_PACKET_REPLAY_RETURN_LOOKUP_TICK -ErrorAction SilentlyContinue
        }
        if ($PacketBridgeMaintainPacketFreeBytes) {
            $env:MELONDS_NSML_PACKET_BRIDGE_MAINTAIN_PACKET_FREE_BYTES = "1"
        } else {
            Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_MAINTAIN_PACKET_FREE_BYTES -ErrorAction SilentlyContinue
        }
        if ($PacketBridgeMaintainSessionPeers) {
            $env:MELONDS_NSML_PACKET_BRIDGE_MAINTAIN_SESSION_PEERS = "1"
            $env:MELONDS_NSML_PACKET_BRIDGE_MAINTAIN_SESSION_PEERS_START_FRAME = "$PacketBridgeMaintainSessionPeersStartFrame"
            if ($PacketBridgeMaintainSessionPeersHostOnly) { $env:MELONDS_NSML_PACKET_BRIDGE_MAINTAIN_SESSION_PEERS_HOST_ONLY = "1" } else { Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_MAINTAIN_SESSION_PEERS_HOST_ONLY -ErrorAction SilentlyContinue }
            if ($PacketBridgeMaintainSessionPeersClientOnly) { $env:MELONDS_NSML_PACKET_BRIDGE_MAINTAIN_SESSION_PEERS_CLIENT_ONLY = "1" } else { Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_MAINTAIN_SESSION_PEERS_CLIENT_ONLY -ErrorAction SilentlyContinue }
        } else {
            Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_MAINTAIN_SESSION_PEERS -ErrorAction SilentlyContinue
            Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_MAINTAIN_SESSION_PEERS_START_FRAME -ErrorAction SilentlyContinue
            Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_MAINTAIN_SESSION_PEERS_HOST_ONLY -ErrorAction SilentlyContinue
            Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_MAINTAIN_SESSION_PEERS_CLIENT_ONLY -ErrorAction SilentlyContinue
        }
        if ($PacketBridgeLowerStatusResult -ge 0) {
            $env:MELONDS_NSML_PACKET_BRIDGE_LOWER_STATUS_RESULT = "$PacketBridgeLowerStatusResult"
        } else {
            Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_LOWER_STATUS_RESULT -ErrorAction SilentlyContinue
        }
        if ($PacketBridgeClientConfirmToStageStart) {
            $env:MELONDS_NSML_PACKET_BRIDGE_CLIENT_CONFIRM_TO_STAGE_START = "1"
            $env:MELONDS_NSML_PACKET_BRIDGE_CLIENT_CONFIRM_TO_STAGE_START_FRAME = "$PacketBridgeClientConfirmToStageStartFrame"
        } else {
            Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_CLIENT_CONFIRM_TO_STAGE_START -ErrorAction SilentlyContinue
            Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_CLIENT_CONFIRM_TO_STAGE_START_FRAME -ErrorAction SilentlyContinue
        }
        if ($PacketBridgeStageStartReadyProbe) {
            $env:MELONDS_NSML_PACKET_BRIDGE_STAGE_START_READY_PROBE = "1"
            if ($PacketBridgeStageStartPacketAction -ge 0) { $env:MELONDS_NSML_PACKET_BRIDGE_STAGE_START_PACKET_ACTION = "$PacketBridgeStageStartPacketAction" } else { Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_STAGE_START_PACKET_ACTION -ErrorAction SilentlyContinue }
            if ($PacketBridgeStageStartNet14 -ge 0) { $env:MELONDS_NSML_PACKET_BRIDGE_STAGE_START_NET14 = "$PacketBridgeStageStartNet14" } else { Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_STAGE_START_NET14 -ErrorAction SilentlyContinue }
            if ($PacketBridgeStageStartNet1C -ge 0) { $env:MELONDS_NSML_PACKET_BRIDGE_STAGE_START_NET1C = "$PacketBridgeStageStartNet1C" } else { Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_STAGE_START_NET1C -ErrorAction SilentlyContinue }
            if ($PacketBridgeStageStartNet20 -ge 0) { $env:MELONDS_NSML_PACKET_BRIDGE_STAGE_START_NET20 = "$PacketBridgeStageStartNet20" } else { Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_STAGE_START_NET20 -ErrorAction SilentlyContinue }
            if ($PacketBridgeStageStartNet20Step3 -ge 0) { $env:MELONDS_NSML_PACKET_BRIDGE_STAGE_START_NET20_STEP3 = "$PacketBridgeStageStartNet20Step3" } else { Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_STAGE_START_NET20_STEP3 -ErrorAction SilentlyContinue }
            if ($PacketBridgeStageStartNet20Step3MinTimer -ge 0) { $env:MELONDS_NSML_PACKET_BRIDGE_STAGE_START_NET20_STEP3_MIN_TIMER = "$PacketBridgeStageStartNet20Step3MinTimer" } else { Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_STAGE_START_NET20_STEP3_MIN_TIMER -ErrorAction SilentlyContinue }
            if ($PacketBridgeStageStartNet20Check) { $env:MELONDS_NSML_PACKET_BRIDGE_STAGE_START_NET20_CHECK = "1" } else { Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_STAGE_START_NET20_CHECK -ErrorAction SilentlyContinue }
            if ($PacketBridgeStageStartNet20CheckMinTimer -ge 0) { $env:MELONDS_NSML_PACKET_BRIDGE_STAGE_START_NET20_CHECK_MIN_TIMER = "$PacketBridgeStageStartNet20CheckMinTimer" } else { Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_STAGE_START_NET20_CHECK_MIN_TIMER -ErrorAction SilentlyContinue }
            if ($PacketBridgeStageStartStep6Close) { $env:MELONDS_NSML_PACKET_BRIDGE_STAGE_START_STEP6_CLOSE = "1" } else { Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_STAGE_START_STEP6_CLOSE -ErrorAction SilentlyContinue }
            if ($PacketBridgeStageStartStep6CloseMinTimer -ge 0) { $env:MELONDS_NSML_PACKET_BRIDGE_STAGE_START_STEP6_CLOSE_MIN_TIMER = "$PacketBridgeStageStartStep6CloseMinTimer" } else { Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_STAGE_START_STEP6_CLOSE_MIN_TIMER -ErrorAction SilentlyContinue }
            if ($PacketBridgeStageSceneReadyClose) { $env:MELONDS_NSML_PACKET_BRIDGE_STAGE_SCENE_READY_CLOSE = "1" } else { Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_STAGE_SCENE_READY_CLOSE -ErrorAction SilentlyContinue }
            if ($PacketBridgeStageSceneReadyCloseStartFrame -ge 0) { $env:MELONDS_NSML_PACKET_BRIDGE_STAGE_SCENE_READY_CLOSE_START_FRAME = "$PacketBridgeStageSceneReadyCloseStartFrame" } else { Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_STAGE_SCENE_READY_CLOSE_START_FRAME -ErrorAction SilentlyContinue }
            if ($PacketBridgeReadPacketByte) { $env:MELONDS_NSML_PACKET_BRIDGE_READ_PACKET_BYTE = "1" } else { Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_READ_PACKET_BYTE -ErrorAction SilentlyContinue }
            if ($PacketBridgeCheckPacketBits) { $env:MELONDS_NSML_PACKET_BRIDGE_CHECK_PACKET_BITS = "1" } else { Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_CHECK_PACKET_BITS -ErrorAction SilentlyContinue }
            if ($PacketBridgeStageStartNet24 -ge 0) { $env:MELONDS_NSML_PACKET_BRIDGE_STAGE_START_NET24 = "$PacketBridgeStageStartNet24" } else { Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_STAGE_START_NET24 -ErrorAction SilentlyContinue }
            if ($PacketBridgeStageStartNet2C -ge 0) { $env:MELONDS_NSML_PACKET_BRIDGE_STAGE_START_NET2C = "$PacketBridgeStageStartNet2C" } else { Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_STAGE_START_NET2C -ErrorAction SilentlyContinue }
            if ($PacketBridgeStageStartNet34 -ge 0) { $env:MELONDS_NSML_PACKET_BRIDGE_STAGE_START_NET34 = "$PacketBridgeStageStartNet34" } else { Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_STAGE_START_NET34 -ErrorAction SilentlyContinue }
        } else {
            Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_STAGE_START_READY_PROBE -ErrorAction SilentlyContinue
            Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_STAGE_START_PACKET_ACTION -ErrorAction SilentlyContinue
            Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_STAGE_START_NET14 -ErrorAction SilentlyContinue
            Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_STAGE_START_NET1C -ErrorAction SilentlyContinue
            Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_STAGE_START_NET20 -ErrorAction SilentlyContinue
            Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_STAGE_START_NET20_STEP3 -ErrorAction SilentlyContinue
            Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_STAGE_START_NET20_STEP3_MIN_TIMER -ErrorAction SilentlyContinue
            Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_STAGE_START_NET20_CHECK -ErrorAction SilentlyContinue
            Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_STAGE_START_NET20_CHECK_MIN_TIMER -ErrorAction SilentlyContinue
            Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_STAGE_START_STEP6_CLOSE -ErrorAction SilentlyContinue
            Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_STAGE_START_STEP6_CLOSE_MIN_TIMER -ErrorAction SilentlyContinue
            Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_STAGE_SCENE_READY_CLOSE -ErrorAction SilentlyContinue
            Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_STAGE_SCENE_READY_CLOSE_START_FRAME -ErrorAction SilentlyContinue
            Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_STAGE_START_NET24 -ErrorAction SilentlyContinue
            Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_STAGE_START_NET2C -ErrorAction SilentlyContinue
            Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_STAGE_START_NET34 -ErrorAction SilentlyContinue
        }
        if ($PacketBridgeRunStageStartSMUpdate) {
            $env:MELONDS_NSML_PACKET_BRIDGE_RUN_STAGE_START_SM_UPDATE = "1"
            $env:MELONDS_NSML_PACKET_BRIDGE_RUN_STAGE_START_SM_UPDATE_START_FRAME = "$PacketBridgeRunStageStartSMUpdateStartFrame"
        } else {
            Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_RUN_STAGE_START_SM_UPDATE -ErrorAction SilentlyContinue
            Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_RUN_STAGE_START_SM_UPDATE_START_FRAME -ErrorAction SilentlyContinue
        }
        if ($PacketBridgeRunVSConnectOnUpdate) {
            $env:MELONDS_NSML_PACKET_BRIDGE_RUN_VSCONNECT_ON_UPDATE = "1"
            $env:MELONDS_NSML_PACKET_BRIDGE_RUN_VSCONNECT_ON_UPDATE_START_FRAME = "$PacketBridgeRunVSConnectOnUpdateStartFrame"
        } else {
            Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_RUN_VSCONNECT_ON_UPDATE -ErrorAction SilentlyContinue
            Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_RUN_VSCONNECT_ON_UPDATE_START_FRAME -ErrorAction SilentlyContinue
        }
        if ($StageStartDispatchTrace) {
            $env:MELONDS_NSML_STAGE_START_DISPATCH_TRACE = "1"
            $env:MELONDS_NSML_STAGE_START_DISPATCH_TRACE_LOG = "$Stdout.stage-start-dispatch.csv"
            if ($StageStartDispatchTraceFull) { $env:MELONDS_NSML_STAGE_START_DISPATCH_TRACE_FULL = "1" } else { Remove-Item Env:\MELONDS_NSML_STAGE_START_DISPATCH_TRACE_FULL -ErrorAction SilentlyContinue }
            if ($StageStartDispatchTraceStartFrame -gt 0) { $env:MELONDS_NSML_STAGE_START_DISPATCH_TRACE_START_FRAME = "$StageStartDispatchTraceStartFrame" } else { Remove-Item Env:\MELONDS_NSML_STAGE_START_DISPATCH_TRACE_START_FRAME -ErrorAction SilentlyContinue }
            if ($StageStartDispatchTraceEndFrame -gt 0) { $env:MELONDS_NSML_STAGE_START_DISPATCH_TRACE_END_FRAME = "$StageStartDispatchTraceEndFrame" } else { Remove-Item Env:\MELONDS_NSML_STAGE_START_DISPATCH_TRACE_END_FRAME -ErrorAction SilentlyContinue }
        } else {
            Remove-Item Env:\MELONDS_NSML_STAGE_START_DISPATCH_TRACE -ErrorAction SilentlyContinue
            Remove-Item Env:\MELONDS_NSML_STAGE_START_DISPATCH_TRACE_LOG -ErrorAction SilentlyContinue
            Remove-Item Env:\MELONDS_NSML_STAGE_START_DISPATCH_TRACE_FULL -ErrorAction SilentlyContinue
            Remove-Item Env:\MELONDS_NSML_STAGE_START_DISPATCH_TRACE_START_FRAME -ErrorAction SilentlyContinue
            Remove-Item Env:\MELONDS_NSML_STAGE_START_DISPATCH_TRACE_END_FRAME -ErrorAction SilentlyContinue
        }
        if ($PacketBridgeReplayOps) {
            $env:MELONDS_NSML_PACKET_REPLAY_OPS = $PacketBridgeReplayOps
        } else {
            Remove-Item Env:\MELONDS_NSML_PACKET_REPLAY_OPS -ErrorAction SilentlyContinue
        }
        Remove-Item Env:\MELONDS_NSML_WAIT_FOR_PEER -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_SEED_WAIT_TIMEOUT_MS -ErrorAction SilentlyContinue
        if ($PacketBridgeStartFrame -gt 0) {
            $env:MELONDS_NSML_DEFER_NETWORK_UNTIL_START = "1"
            $env:MELONDS_NSML_NETPLAY_START_FRAME = "$PacketBridgeStartFrame"
        } else {
            Remove-Item Env:\MELONDS_NSML_DEFER_NETWORK_UNTIL_START -ErrorAction SilentlyContinue
            Remove-Item Env:\MELONDS_NSML_NETPLAY_START_FRAME -ErrorAction SilentlyContinue
        }
        if ($Role -eq "client") {
            $env:MELONDS_NSML_PEER = $Peer
        } else {
            Remove-Item Env:\MELONDS_NSML_PEER -ErrorAction SilentlyContinue
        }
        if ($PacketBridgeTrace) {
            $env:MELONDS_NSML_PACKET_BRIDGE_TRACE = "1"
            $env:MELONDS_NSML_PACKET_REPLAY_LOG = "$Stdout.packet-replay.csv"
        } else {
            Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_TRACE -ErrorAction SilentlyContinue
            if (-not $PacketReplayFile) {
                Remove-Item Env:\MELONDS_NSML_PACKET_REPLAY_LOG -ErrorAction SilentlyContinue
            }
        }
    } else {
        Remove-Item Env:\MELONDS_NSML_POC -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_ROLE -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_PEER -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_PORT -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_LOCAL_INSTANCE -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_ONLY -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_ALLOW_JIT -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_LOCAL_PLAYER -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_LOAD_LEVEL_PLAYER_ID -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_FORCE_GAME_LOCAL_PLAYER_ID -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_FORCE_GAME_LOCAL_PLAYER_ID_START_FRAME -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_FORCE_GAME_LOCAL_PLAYER_ID_EARLY -ErrorAction SilentlyContinue
        if (-not ($PacketCapture -and $PacketCaptureAllowPreGame)) {
            Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_ALLOW_PRE_GAME -ErrorAction SilentlyContinue
        }
        Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_REPLAY_TICK_OFFSET -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_WAIT -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_WAIT_TIMEOUT_MS -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_WAIT_START_FRAME -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_WAIT_TICK_AHEAD -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_DIRECT_CAPTURE -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_FAKE_PEER_INFO -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_BYPASS_START_CONNECTION -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_BYPASS_START_CONNECTION_START_FRAME -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_BYPASS_START_CONNECTION_CLIENT_ONLY -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_BYPASS_WIFI_START -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_BYPASS_WIFI_START_START_FRAME -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_LOWER_STATUS_RESULT -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_CLIENT_CONFIRM_TO_STAGE_START -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_CLIENT_CONFIRM_TO_STAGE_START_FRAME -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_FORCE_TICK -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_FORCE_TICK_START_FRAME -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_FORCE_TICK_BASE -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_FORCE_NET_READY -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_FORCE_NET_READY_START_FRAME -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_FORCE_NET_READY_END_FRAME -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_FORCE_LOAD_GAME_SM -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_FORCE_LOAD_GAME_SM_START_FRAME -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_FORCE_LOAD_GAME_SM_STEP -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_FORCE_LOAD_GAME_SM_TIMER -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_FORCE_LOAD_GAME_SM_RUN_UPDATE -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_FORCE_LOAD_GAME_SM_RUN_UPDATE_ALL -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_FORCE_LOAD_GAME_SM_PRELOAD -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_SAFE_CREATE_LOAD_GAME_CALL -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_SAFE_CREATE_LOAD_GAME_CALL_FRAME -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_SAFE_CREATE_LOAD_GAME_CALL_PC -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_SCHEDULE_LOAD_GAME_SM -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_SAFE_SCHEDULE_LOAD_GAME_CALL -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_SAFE_SCHEDULE_LOAD_GAME_CALL_FRAME -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_SAFE_SCHEDULE_LOAD_GAME_CALL_PC -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_FORCE_SCHEDULE_LOAD_GAME_SM_ARGS -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_FORCE_SCHEDULE_LOAD_GAME_SM_ARGS_FRAME -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_FORCE_MVL_FILE_CACHE -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_PACKET_REPLAY_LOOKUP_TICK_DELAY -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_MAX_PUMP_EVENTS -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_SUPPRESS_DISCONNECT -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_SUPPRESS_BLACKOUT -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_PRESERVE_NET_POINTERS -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_BYPASS_NET_RESET -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_BYPASS_NET_DISCONNECT -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_BYPASS_NET_DISCONNECT_MODE -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_BYPASS_NET_DISCONNECT_START_FRAME -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_FORCE_TRANSFER_RESULT -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_FORCE_TRANSFER_CLIENT_ONLY -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_FORCE_TRANSFER_START_FRAME -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_FORCE_TRANSFER_RESULT_VALUE -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_MAX_TICK_LEAD -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_THROTTLE_TIMEOUT_MS -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_LOCAL_INPUT_DELAY -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_NEUTRALIZE_LOCAL_INPUT -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_PRESERVE_LOCAL_TOUCH -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_SEND_DELAY_FRAMES -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_SEND_JITTER_FRAMES -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_NET_RANDOM_VALUE -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_NET_RANDOM_FRAME -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_NET_RANDOM_AUTO -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_PACKET_REPLAY_STRICT -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_PACKET_REPLAY_STRICT_PLAYERS -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_PACKET_REPLAY_STRICT_START_FRAME -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_PACKET_REPLAY_STRICT_REQUIRE_LEAD -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_PACKET_REPLAY_LIVE_FALLBACK_WINDOW -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_PACKET_REPLAY_LIVE_FALLBACK_NEAREST -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_PACKET_REPLAY_RETURN_LOOKUP_TICK -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_MAINTAIN_PACKET_FREE_BYTES -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_MAINTAIN_SESSION_PEERS -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_MAINTAIN_SESSION_PEERS_START_FRAME -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_MAINTAIN_SESSION_PEERS_HOST_ONLY -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_MAINTAIN_SESSION_PEERS_CLIENT_ONLY -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_STAGE_START_READY_PROBE -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_STAGE_START_PACKET_ACTION -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_STAGE_START_NET14 -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_STAGE_START_NET1C -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_STAGE_START_NET20 -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_STAGE_START_NET20_STEP3 -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_STAGE_START_NET20_STEP3_MIN_TIMER -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_STAGE_START_NET20_CHECK -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_STAGE_START_NET20_CHECK_MIN_TIMER -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_STAGE_START_STEP6_CLOSE -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_STAGE_START_STEP6_CLOSE_MIN_TIMER -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_STAGE_SCENE_READY_CLOSE -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_STAGE_SCENE_READY_CLOSE_START_FRAME -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_STAGE_START_NET24 -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_STAGE_START_NET2C -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_STAGE_START_NET34 -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_RUN_STAGE_START_SM_UPDATE -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_RUN_STAGE_START_SM_UPDATE_START_FRAME -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_RUN_VSCONNECT_ON_UPDATE -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_RUN_VSCONNECT_ON_UPDATE_START_FRAME -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_STAGE_START_DISPATCH_TRACE -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_STAGE_START_DISPATCH_TRACE_LOG -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_STAGE_START_DISPATCH_TRACE_START_FRAME -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_STAGE_START_DISPATCH_TRACE_END_FRAME -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_PACKET_REPLAY_OPS -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_TRACE -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_WAIT_FOR_PEER -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_SEED_WAIT_TIMEOUT_MS -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_DEFER_NETWORK_UNTIL_START -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_NETPLAY_START_FRAME -ErrorAction SilentlyContinue
    }
    if (-not $PacketBridge) {
        Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_LOCAL_INPUT_DELAY -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_NEUTRALIZE_LOCAL_INPUT -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_PRESERVE_LOCAL_TOUCH -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_SEND_DELAY_FRAMES -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_SEND_JITTER_FRAMES -ErrorAction SilentlyContinue
        if ($PacketBridgeLookupTickDelay -gt 0) {
            $env:MELONDS_NSML_PACKET_REPLAY_LOOKUP_TICK_DELAY = "$PacketBridgeLookupTickDelay"
        }
        if ($PacketBridgeStrictRemote -or $PacketBridgeStrictPlayers) {
            $env:MELONDS_NSML_PACKET_REPLAY_STRICT = "1"
            if ($PacketBridgeStrictStartFrame -gt 0) {
                $env:MELONDS_NSML_PACKET_REPLAY_STRICT_START_FRAME = "$PacketBridgeStrictStartFrame"
            }
            if ($PacketBridgeStrictRequireLead -gt 0) {
                $env:MELONDS_NSML_PACKET_REPLAY_STRICT_REQUIRE_LEAD = "$PacketBridgeStrictRequireLead"
            }
            if ($PacketBridgeStrictPlayers) {
                $env:MELONDS_NSML_PACKET_REPLAY_STRICT_PLAYERS = $PacketBridgeStrictPlayers
            } elseif ($Role -eq "host") {
                $env:MELONDS_NSML_PACKET_REPLAY_STRICT_PLAYERS = "1"
            } else {
                $env:MELONDS_NSML_PACKET_REPLAY_STRICT_PLAYERS = "0"
            }
        }
        if ($PacketBridgeLiveFallbackWindow -gt 0) {
            $env:MELONDS_NSML_PACKET_REPLAY_LIVE_FALLBACK_WINDOW = "$PacketBridgeLiveFallbackWindow"
        }
        if ($PacketBridgeLiveFallbackNearest) {
            $env:MELONDS_NSML_PACKET_REPLAY_LIVE_FALLBACK_NEAREST = "1"
        }
        if ($PacketBridgeLiveFallbackLatestBefore) {
            $env:MELONDS_NSML_PACKET_REPLAY_LIVE_FALLBACK_LATEST_BEFORE = "1"
        }
        if ($PacketBridgeLiveFallbackStartFrame -gt 0) {
            $env:MELONDS_NSML_PACKET_REPLAY_LIVE_FALLBACK_START_FRAME = "$PacketBridgeLiveFallbackStartFrame"
        }
        if ($PacketBridgeReplayReturnLookupTick) {
            $env:MELONDS_NSML_PACKET_REPLAY_RETURN_LOOKUP_TICK = "1"
        }
        if ($PacketBridgeMaintainPacketFreeBytes) {
            $env:MELONDS_NSML_PACKET_BRIDGE_MAINTAIN_PACKET_FREE_BYTES = "1"
        }
        if ($PacketBridgeMaintainSessionPeers) {
            $env:MELONDS_NSML_PACKET_BRIDGE_MAINTAIN_SESSION_PEERS = "1"
            $env:MELONDS_NSML_PACKET_BRIDGE_MAINTAIN_SESSION_PEERS_START_FRAME = "$PacketBridgeMaintainSessionPeersStartFrame"
            if ($PacketBridgeMaintainSessionPeersHostOnly) { $env:MELONDS_NSML_PACKET_BRIDGE_MAINTAIN_SESSION_PEERS_HOST_ONLY = "1" }
            if ($PacketBridgeMaintainSessionPeersClientOnly) { $env:MELONDS_NSML_PACKET_BRIDGE_MAINTAIN_SESSION_PEERS_CLIENT_ONLY = "1" }
        }
        if ($PacketBridgeLowerStatusResult -ge 0) {
            $env:MELONDS_NSML_PACKET_BRIDGE_LOWER_STATUS_RESULT = "$PacketBridgeLowerStatusResult"
        }
        if ($PacketBridgeClientConfirmToStageStart) {
            $env:MELONDS_NSML_PACKET_BRIDGE_CLIENT_CONFIRM_TO_STAGE_START = "1"
            $env:MELONDS_NSML_PACKET_BRIDGE_CLIENT_CONFIRM_TO_STAGE_START_FRAME = "$PacketBridgeClientConfirmToStageStartFrame"
        }
        if ($PacketBridgeStageStartReadyProbe) {
            $env:MELONDS_NSML_PACKET_BRIDGE_STAGE_START_READY_PROBE = "1"
            if ($PacketBridgeStageStartPacketAction -ge 0) { $env:MELONDS_NSML_PACKET_BRIDGE_STAGE_START_PACKET_ACTION = "$PacketBridgeStageStartPacketAction" }
            if ($PacketBridgeStageStartNet14 -ge 0) { $env:MELONDS_NSML_PACKET_BRIDGE_STAGE_START_NET14 = "$PacketBridgeStageStartNet14" }
            if ($PacketBridgeStageStartNet1C -ge 0) { $env:MELONDS_NSML_PACKET_BRIDGE_STAGE_START_NET1C = "$PacketBridgeStageStartNet1C" }
            if ($PacketBridgeStageStartNet20 -ge 0) { $env:MELONDS_NSML_PACKET_BRIDGE_STAGE_START_NET20 = "$PacketBridgeStageStartNet20" }
            if ($PacketBridgeStageStartNet20Step3 -ge 0) { $env:MELONDS_NSML_PACKET_BRIDGE_STAGE_START_NET20_STEP3 = "$PacketBridgeStageStartNet20Step3" }
            if ($PacketBridgeStageStartNet20Step3MinTimer -ge 0) { $env:MELONDS_NSML_PACKET_BRIDGE_STAGE_START_NET20_STEP3_MIN_TIMER = "$PacketBridgeStageStartNet20Step3MinTimer" }
            if ($PacketBridgeStageStartNet20Check) { $env:MELONDS_NSML_PACKET_BRIDGE_STAGE_START_NET20_CHECK = "1" }
            if ($PacketBridgeStageStartNet20CheckMinTimer -ge 0) { $env:MELONDS_NSML_PACKET_BRIDGE_STAGE_START_NET20_CHECK_MIN_TIMER = "$PacketBridgeStageStartNet20CheckMinTimer" }
            if ($PacketBridgeStageStartStep6Close) { $env:MELONDS_NSML_PACKET_BRIDGE_STAGE_START_STEP6_CLOSE = "1" }
            if ($PacketBridgeStageStartStep6CloseMinTimer -ge 0) { $env:MELONDS_NSML_PACKET_BRIDGE_STAGE_START_STEP6_CLOSE_MIN_TIMER = "$PacketBridgeStageStartStep6CloseMinTimer" }
            if ($PacketBridgeStageSceneReadyClose) { $env:MELONDS_NSML_PACKET_BRIDGE_STAGE_SCENE_READY_CLOSE = "1" }
            if ($PacketBridgeStageSceneReadyCloseStartFrame -ge 0) { $env:MELONDS_NSML_PACKET_BRIDGE_STAGE_SCENE_READY_CLOSE_START_FRAME = "$PacketBridgeStageSceneReadyCloseStartFrame" }
            if ($PacketBridgeReadPacketByte) { $env:MELONDS_NSML_PACKET_BRIDGE_READ_PACKET_BYTE = "1" }
            if ($PacketBridgeCheckPacketBits) { $env:MELONDS_NSML_PACKET_BRIDGE_CHECK_PACKET_BITS = "1" }
            if ($PacketBridgeStageStartNet24 -ge 0) { $env:MELONDS_NSML_PACKET_BRIDGE_STAGE_START_NET24 = "$PacketBridgeStageStartNet24" }
            if ($PacketBridgeStageStartNet2C -ge 0) { $env:MELONDS_NSML_PACKET_BRIDGE_STAGE_START_NET2C = "$PacketBridgeStageStartNet2C" }
            if ($PacketBridgeStageStartNet34 -ge 0) { $env:MELONDS_NSML_PACKET_BRIDGE_STAGE_START_NET34 = "$PacketBridgeStageStartNet34" }
        }
        if ($PacketBridgeRunStageStartSMUpdate) {
            $env:MELONDS_NSML_PACKET_BRIDGE_RUN_STAGE_START_SM_UPDATE = "1"
            $env:MELONDS_NSML_PACKET_BRIDGE_RUN_STAGE_START_SM_UPDATE_START_FRAME = "$PacketBridgeRunStageStartSMUpdateStartFrame"
        }
        if ($PacketBridgeRunVSConnectOnUpdate) {
            $env:MELONDS_NSML_PACKET_BRIDGE_RUN_VSCONNECT_ON_UPDATE = "1"
            $env:MELONDS_NSML_PACKET_BRIDGE_RUN_VSCONNECT_ON_UPDATE_START_FRAME = "$PacketBridgeRunVSConnectOnUpdateStartFrame"
        }
        if ($StageStartDispatchTrace) {
            $env:MELONDS_NSML_STAGE_START_DISPATCH_TRACE = "1"
            $env:MELONDS_NSML_STAGE_START_DISPATCH_TRACE_LOG = "$Stdout.stage-start-dispatch.csv"
            if ($StageStartDispatchTraceFull) { $env:MELONDS_NSML_STAGE_START_DISPATCH_TRACE_FULL = "1" }
            if ($StageStartDispatchTraceStartFrame -gt 0) { $env:MELONDS_NSML_STAGE_START_DISPATCH_TRACE_START_FRAME = "$StageStartDispatchTraceStartFrame" }
            if ($StageStartDispatchTraceEndFrame -gt 0) { $env:MELONDS_NSML_STAGE_START_DISPATCH_TRACE_END_FRAME = "$StageStartDispatchTraceEndFrame" }
        }
        if ($PacketBridgeReplayOps) {
            $env:MELONDS_NSML_PACKET_REPLAY_OPS = $PacketBridgeReplayOps
        }
        if ($PacketBridgeAllowPreGame) {
            $env:MELONDS_NSML_PACKET_BRIDGE_ALLOW_PRE_GAME = "1"
        }
        if ($PacketBridgeForceNetReady) {
            $env:MELONDS_NSML_PACKET_BRIDGE_FORCE_NET_READY = "1"
            $env:MELONDS_NSML_PACKET_BRIDGE_FORCE_NET_READY_START_FRAME = "$PacketBridgeForceNetReadyStartFrame"
            if ($PacketBridgeForceNetReadyEndFrame -gt 0) { $env:MELONDS_NSML_PACKET_BRIDGE_FORCE_NET_READY_END_FRAME = "$PacketBridgeForceNetReadyEndFrame" } else { Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_FORCE_NET_READY_END_FRAME -ErrorAction SilentlyContinue }
            if ($PacketBridgeForceNetReadyHostOnly) { $env:MELONDS_NSML_PACKET_BRIDGE_FORCE_NET_READY_HOST_ONLY = "1" } else { Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_FORCE_NET_READY_HOST_ONLY -ErrorAction SilentlyContinue }
            if ($PacketBridgeForceNetReadyClientOnly) { $env:MELONDS_NSML_PACKET_BRIDGE_FORCE_NET_READY_CLIENT_ONLY = "1" } else { Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_FORCE_NET_READY_CLIENT_ONLY -ErrorAction SilentlyContinue }
            if ($PacketBridgeForceStageSceneArg) { $env:MELONDS_NSML_PACKET_BRIDGE_FORCE_STAGE_SCENE_ARG = "1" } else { Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_FORCE_STAGE_SCENE_ARG -ErrorAction SilentlyContinue }
            if ($PacketBridgeForceNetReadyState10) { $env:MELONDS_NSML_PACKET_BRIDGE_FORCE_NET_READY_STATE10 = "1" } else { Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_FORCE_NET_READY_STATE10 -ErrorAction SilentlyContinue }
            if ($PacketBridgeForceNetReadyState10ClientOnly) { $env:MELONDS_NSML_PACKET_BRIDGE_FORCE_NET_READY_STATE10_CLIENT_ONLY = "1" } else { Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_FORCE_NET_READY_STATE10_CLIENT_ONLY -ErrorAction SilentlyContinue }
            if ($PacketBridgeForceCourseSelectReady) { $env:MELONDS_NSML_PACKET_BRIDGE_FORCE_COURSE_SELECT_READY = "1" } else { Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_FORCE_COURSE_SELECT_READY -ErrorAction SilentlyContinue }
        }
        if ($PacketBridgeForceCourseSelectReady) {
            $env:MELONDS_NSML_PACKET_BRIDGE_FORCE_COURSE_SELECT_READY = "1"
            $env:MELONDS_NSML_PACKET_BRIDGE_FORCE_COURSE_SELECT_READY_START_FRAME = "$PacketBridgeForceCourseSelectReadyStartFrame"
        } else {
            Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_FORCE_COURSE_SELECT_READY -ErrorAction SilentlyContinue
            Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_FORCE_COURSE_SELECT_READY_START_FRAME -ErrorAction SilentlyContinue
        }
        if ($PacketBridgeForceLoadGameSM) {
            $env:MELONDS_NSML_PACKET_BRIDGE_FORCE_LOAD_GAME_SM = "1"
            $env:MELONDS_NSML_PACKET_BRIDGE_FORCE_LOAD_GAME_SM_START_FRAME = "$PacketBridgeForceLoadGameSMStartFrame"
            $env:MELONDS_NSML_PACKET_BRIDGE_FORCE_LOAD_GAME_SM_STEP = "$PacketBridgeForceLoadGameSMStep"
            if ($PacketBridgeForceLoadGameSMTimer -ge 0) {
                $env:MELONDS_NSML_PACKET_BRIDGE_FORCE_LOAD_GAME_SM_TIMER = "$PacketBridgeForceLoadGameSMTimer"
            }
            if ($PacketBridgeForceLoadGameSMFlags -ge 0) {
                $env:MELONDS_NSML_PACKET_BRIDGE_FORCE_LOAD_GAME_SM_FLAGS = "$PacketBridgeForceLoadGameSMFlags"
            }
            if ($PacketBridgeForceLoadGameSMRunUpdate) {
                $env:MELONDS_NSML_PACKET_BRIDGE_FORCE_LOAD_GAME_SM_RUN_UPDATE = "1"
            }
            if ($PacketBridgeForceLoadGameSMRunUpdateAll) {
                $env:MELONDS_NSML_PACKET_BRIDGE_FORCE_LOAD_GAME_SM_RUN_UPDATE_ALL = "1"
            }
            if ($PacketBridgeForceLoadGameSMPulseAction) {
                $env:MELONDS_NSML_PACKET_BRIDGE_FORCE_LOAD_GAME_SM_PULSE_ACTION = "1"
            }
            if ($PacketBridgeForceLoadGameSMBaselineFlags) {
                $env:MELONDS_NSML_PACKET_BRIDGE_FORCE_LOAD_GAME_SM_BASELINE_FLAGS = "1"
            }
            if ($PacketBridgeForceLoadGameSMPreload) {
                $env:MELONDS_NSML_PACKET_BRIDGE_FORCE_LOAD_GAME_SM_PRELOAD = "1"
            }
            if ($PacketBridgeForceLoadGameSMAllowCourseSelect) {
                $env:MELONDS_NSML_PACKET_BRIDGE_FORCE_LOAD_GAME_SM_ALLOW_COURSE_SELECT = "1"
            }
            if ($PacketBridgeForceStagePacketWords) {
                $env:MELONDS_NSML_PACKET_BRIDGE_FORCE_STAGE_PACKET_WORDS = "1"
                $env:MELONDS_NSML_PACKET_BRIDGE_FORCE_STAGE_PACKET_WORDS_START_FRAME = "$PacketBridgeForceStagePacketWordsStartFrame"
                if ($PacketBridgeForceStagePacketWordsEndFrame -gt 0) {
                    $env:MELONDS_NSML_PACKET_BRIDGE_FORCE_STAGE_PACKET_WORDS_END_FRAME = "$PacketBridgeForceStagePacketWordsEndFrame"
                }
                if ($PacketBridgeForceStageNet20OnStageScene) {
                    $env:MELONDS_NSML_PACKET_BRIDGE_FORCE_STAGE_NET20_ON_STAGE_SCENE = "1"
                }
            }
            if ($PacketBridgeScheduleLoadGameSM) {
                $env:MELONDS_NSML_PACKET_BRIDGE_SCHEDULE_LOAD_GAME_SM = "1"
            } else {
                Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_SCHEDULE_LOAD_GAME_SM -ErrorAction SilentlyContinue
            }
            if ($PacketBridgeForceMvlFileCache) {
                $env:MELONDS_NSML_PACKET_BRIDGE_FORCE_MVL_FILE_CACHE = "1"
                $env:MELONDS_NSML_PACKET_BRIDGE_FORCE_MVL_FILE_CACHE_START_FRAME = "$PacketBridgeForceMvlFileCacheStartFrame"
            }
            if ($PacketBridgeForceMvlLoadThread) {
                $env:MELONDS_NSML_PACKET_BRIDGE_FORCE_MVL_LOAD_THREAD = "1"
                $env:MELONDS_NSML_PACKET_BRIDGE_FORCE_MVL_LOAD_THREAD_START_FRAME = "$PacketBridgeForceMvlLoadThreadStartFrame"
            } else {
                Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_FORCE_MVL_LOAD_THREAD -ErrorAction SilentlyContinue
                Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_FORCE_MVL_LOAD_THREAD_START_FRAME -ErrorAction SilentlyContinue
            }
        }
        if ($PacketBridgeForceStagePacketWords) {
            $env:MELONDS_NSML_PACKET_BRIDGE_FORCE_STAGE_PACKET_WORDS = "1"
            $env:MELONDS_NSML_PACKET_BRIDGE_FORCE_STAGE_PACKET_WORDS_START_FRAME = "$PacketBridgeForceStagePacketWordsStartFrame"
            if ($PacketBridgeForceStagePacketWordsEndFrame -gt 0) {
                $env:MELONDS_NSML_PACKET_BRIDGE_FORCE_STAGE_PACKET_WORDS_END_FRAME = "$PacketBridgeForceStagePacketWordsEndFrame"
            } else {
                Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_FORCE_STAGE_PACKET_WORDS_END_FRAME -ErrorAction SilentlyContinue
            }
            if ($PacketBridgeForceStageNet20OnStageScene) {
                $env:MELONDS_NSML_PACKET_BRIDGE_FORCE_STAGE_NET20_ON_STAGE_SCENE = "1"
            } else {
                Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_FORCE_STAGE_NET20_ON_STAGE_SCENE -ErrorAction SilentlyContinue
            }
        } else {
            Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_FORCE_STAGE_PACKET_WORDS -ErrorAction SilentlyContinue
            Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_FORCE_STAGE_PACKET_WORDS_START_FRAME -ErrorAction SilentlyContinue
            Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_FORCE_STAGE_PACKET_WORDS_END_FRAME -ErrorAction SilentlyContinue
            Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_FORCE_STAGE_NET20_ON_STAGE_SCENE -ErrorAction SilentlyContinue
        }
        if ($PacketBridgeDummyAlloc) {
            $env:MELONDS_NSML_PACKET_BRIDGE_DUMMY_ALLOC = "1"
            $env:MELONDS_NSML_PACKET_BRIDGE_DUMMY_ALLOC_FRAME = "$PacketBridgeDummyAllocFrame"
            $env:MELONDS_NSML_PACKET_BRIDGE_DUMMY_ALLOC_SIZE = "$PacketBridgeDummyAllocSize"
        } else {
            Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_DUMMY_ALLOC -ErrorAction SilentlyContinue
            Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_DUMMY_ALLOC_FRAME -ErrorAction SilentlyContinue
            Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_DUMMY_ALLOC_SIZE -ErrorAction SilentlyContinue
        }
        if ($PacketBridgeForceTransferResult) {
            $env:MELONDS_NSML_PACKET_BRIDGE_FORCE_TRANSFER_RESULT = "1"
            if ($PacketBridgeForceTransferClientOnly) { $env:MELONDS_NSML_PACKET_BRIDGE_FORCE_TRANSFER_CLIENT_ONLY = "1" } else { Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_FORCE_TRANSFER_CLIENT_ONLY -ErrorAction SilentlyContinue }
            if ($PacketBridgeForceTransferStartFrame -gt 0) {
                $env:MELONDS_NSML_PACKET_BRIDGE_FORCE_TRANSFER_START_FRAME = "$PacketBridgeForceTransferStartFrame"
            } elseif ($DropMPAfterFrame -gt 0) {
                $env:MELONDS_NSML_PACKET_BRIDGE_FORCE_TRANSFER_START_FRAME = "$DropMPAfterFrame"
            }
            $env:MELONDS_NSML_PACKET_BRIDGE_FORCE_TRANSFER_RESULT_VALUE = "$PacketBridgeForceTransferResultValue"
        }
        if ($PacketBridgeTrace) {
            $env:MELONDS_NSML_PACKET_BRIDGE_TRACE = "1"
        }
    }
    if (-not $StateSync) {
        Remove-Item Env:\MELONDS_NSML_STATE_SYNC -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_STATE_APPLY -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_STATE_SYNC_INTERVAL -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_STATE_SYNC_EXTENDED -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_STATE_APPLY_MODE -ErrorAction SilentlyContinue
    }
    if (-not $StateSaveDir) {
        Remove-Item Env:\MELONDS_NSML_STATE_SAVE_DIR -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_STATE_SAVE_FRAME -ErrorAction SilentlyContinue
    }
    if (-not $StateLoadDir) {
        Remove-Item Env:\MELONDS_NSML_STATE_LOAD_DIR -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_STATE_LOAD_FRAME -ErrorAction SilentlyContinue
    }
    $env:MELONDS_NSML_ROLE = $Role
    $env:MELONDS_NSML_FIXED_RTC = "2020-01-01T00:00:00"
    if ($AllowJit -or $PacketBridgeAllowJit) {
        $env:MELONDS_NSML_ALLOW_JIT = "1"
        Remove-Item Env:\MELONDS_NSML_DISABLE_JIT -ErrorAction SilentlyContinue
    } else {
        Remove-Item Env:\MELONDS_NSML_ALLOW_JIT -ErrorAction SilentlyContinue
        $env:MELONDS_NSML_DISABLE_JIT = "1"
    }
    if ($DropMPAfterFrame -gt 0) {
        $env:MELONDS_NSML_DROP_MP_AFTER_FRAME = "$DropMPAfterFrame"
    } else {
        Remove-Item Env:\MELONDS_NSML_DROP_MP_AFTER_FRAME -ErrorAction SilentlyContinue
    }
    if ($NoLanMP) {
        Remove-Item Env:\MELONDS_NSML_MP_INTERFACE -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_LAN_ROLE -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_LAN_PLAYERS -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_LAN_HOST -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_LAN_PLAYER -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_LAN_WAN_MODE -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_LAN_MP_RECV_TIMEOUT_MS -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_LAN_MP_MISC_RECV_TIMEOUT_MS -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_LAN_MP_STALE_MS -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_LAN_MP_REPLY_TIMESTAMP_SLACK_US -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_LAN_MP_SEND_DELAY_MS -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_LAN_MP_RELIABLE -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_LAN_MP_DROP_OLD_REGULAR -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_WIFI_MP_ACCEPT_ANY_CHANNEL -ErrorAction SilentlyContinue
    } else {
        $env:MELONDS_NSML_MP_INTERFACE = "lan"
        $env:MELONDS_NSML_LAN_ROLE = $Role
        $env:MELONDS_NSML_LAN_PLAYERS = "2"
        if ($LanHost) {
            $env:MELONDS_NSML_LAN_HOST = $LanHost
        } else {
            $env:MELONDS_NSML_LAN_HOST = $Peer
        }
        $env:MELONDS_NSML_LAN_PLAYER = "codex-$Role"
        if ($LanWanMode) {
            $env:MELONDS_NSML_LAN_WAN_MODE = "1"
        } else {
            Remove-Item Env:\MELONDS_NSML_LAN_WAN_MODE -ErrorAction SilentlyContinue
        }
        if ($LanMPRecvTimeoutMs -ge 0) {
            $env:MELONDS_NSML_LAN_MP_RECV_TIMEOUT_MS = "$LanMPRecvTimeoutMs"
        } else {
            Remove-Item Env:\MELONDS_NSML_LAN_MP_RECV_TIMEOUT_MS -ErrorAction SilentlyContinue
        }
        if ($LanMPMiscRecvTimeoutMs -ge 0) {
            $env:MELONDS_NSML_LAN_MP_MISC_RECV_TIMEOUT_MS = "$LanMPMiscRecvTimeoutMs"
        } else {
            Remove-Item Env:\MELONDS_NSML_LAN_MP_MISC_RECV_TIMEOUT_MS -ErrorAction SilentlyContinue
        }
        if ($LanMPStaleMs -ge 0) {
            $env:MELONDS_NSML_LAN_MP_STALE_MS = "$LanMPStaleMs"
        } else {
            Remove-Item Env:\MELONDS_NSML_LAN_MP_STALE_MS -ErrorAction SilentlyContinue
        }
        if ($LanMPReplySlackUs -ge 0) {
            $env:MELONDS_NSML_LAN_MP_REPLY_TIMESTAMP_SLACK_US = "$LanMPReplySlackUs"
        } else {
            Remove-Item Env:\MELONDS_NSML_LAN_MP_REPLY_TIMESTAMP_SLACK_US -ErrorAction SilentlyContinue
        }
        $roleSendDelay = $LanMPSendDelayMs
        if ($Role -eq "host" -and $HostLanMPSendDelayMs -ge 0) {
            $roleSendDelay = $HostLanMPSendDelayMs
        } elseif ($Role -eq "client" -and $ClientLanMPSendDelayMs -ge 0) {
            $roleSendDelay = $ClientLanMPSendDelayMs
        }
        if ($roleSendDelay -ge 0) {
            $env:MELONDS_NSML_LAN_MP_SEND_DELAY_MS = "$roleSendDelay"
        } else {
            Remove-Item Env:\MELONDS_NSML_LAN_MP_SEND_DELAY_MS -ErrorAction SilentlyContinue
        }
        if ($LanMPReliable) {
            $env:MELONDS_NSML_LAN_MP_RELIABLE = "1"
        } else {
            Remove-Item Env:\MELONDS_NSML_LAN_MP_RELIABLE -ErrorAction SilentlyContinue
        }
        if ($LanMPDropOldRegular) {
            $env:MELONDS_NSML_LAN_MP_DROP_OLD_REGULAR = "1"
        } else {
            Remove-Item Env:\MELONDS_NSML_LAN_MP_DROP_OLD_REGULAR -ErrorAction SilentlyContinue
        }
        if ($LanMPAcceptAnyChannel) {
            $env:MELONDS_NSML_WIFI_MP_ACCEPT_ANY_CHANNEL = "1"
        } else {
            Remove-Item Env:\MELONDS_NSML_WIFI_MP_ACCEPT_ANY_CHANNEL -ErrorAction SilentlyContinue
        }
    }
    if ($PacketBridge -and $PacketBridgeForceMvlFileCache) {
        $env:MELONDS_NSML_PACKET_BRIDGE_FORCE_MVL_FILE_CACHE = "1"
        $env:MELONDS_NSML_PACKET_BRIDGE_FORCE_MVL_FILE_CACHE_START_FRAME = "$PacketBridgeForceMvlFileCacheStartFrame"
    } else {
        Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_FORCE_MVL_FILE_CACHE -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_FORCE_MVL_FILE_CACHE_START_FRAME -ErrorAction SilentlyContinue
    }
    $forceMvlLoadThreadForRole = $PacketBridgeForceMvlLoadThread
    if ($PacketBridgeForceMvlLoadThreadHostOnly -and $Role -ne "host") {
        $forceMvlLoadThreadForRole = $false
    }
    if ($PacketBridgeForceMvlLoadThreadClientOnly -and $Role -ne "client") {
        $forceMvlLoadThreadForRole = $false
    }
    if ($PacketBridge -and $forceMvlLoadThreadForRole) {
        $env:MELONDS_NSML_PACKET_BRIDGE_FORCE_MVL_LOAD_THREAD = "1"
        $env:MELONDS_NSML_PACKET_BRIDGE_FORCE_MVL_LOAD_THREAD_START_FRAME = "$PacketBridgeForceMvlLoadThreadStartFrame"
    } else {
        Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_FORCE_MVL_LOAD_THREAD -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_PACKET_BRIDGE_FORCE_MVL_LOAD_THREAD_START_FRAME -ErrorAction SilentlyContinue
    }
    if ($Role -eq "host") {
        $env:MELONDS_NSML_FIRMWARE_MAC = "00:09:BF:11:22:33"
    } else {
        $env:MELONDS_NSML_FIRMWARE_MAC = "00:09:BF:11:22:43"
    }
    $roleMemPatchFile = if ($Role -eq "host") { $HostMemPatchFile } else { $ClientMemPatchFile }
    if ($roleMemPatchFile -and $MemPatchFrame -and $MemPatchRanges) {
        $env:MELONDS_NSML_MEM_PATCH_FILE = (Resolve-Path $roleMemPatchFile).Path
        $env:MELONDS_NSML_MEM_PATCH_FRAME = "$MemPatchFrame"
        $env:MELONDS_NSML_MEM_PATCH_RANGES = "$MemPatchRanges"
    } else {
        Remove-Item Env:\MELONDS_NSML_MEM_PATCH_FILE -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_MEM_PATCH_FRAME -ErrorAction SilentlyContinue
        Remove-Item Env:\MELONDS_NSML_MEM_PATCH_RANGES -ErrorAction SilentlyContinue
    }
    @(
        "role=$Role"
        "runRole=$RunRole"
        "peer=$Peer"
        "lanHost=$($env:MELONDS_NSML_LAN_HOST)"
        "packetBridge=$PacketBridge"
        "localInstance=$($env:MELONDS_NSML_LOCAL_INSTANCE)"
        "packetBridgeReplayTickOffset=$($env:MELONDS_NSML_PACKET_BRIDGE_REPLAY_TICK_OFFSET)"
        "packetBridgeLocalPlayer=$($env:MELONDS_NSML_PACKET_BRIDGE_LOCAL_PLAYER)"
        "packetBridgeLoadLevelPlayerID=$($env:MELONDS_NSML_PACKET_BRIDGE_LOAD_LEVEL_PLAYER_ID)"
        "packetBridgeForceGameLocalPlayerID=$($env:MELONDS_NSML_PACKET_BRIDGE_FORCE_GAME_LOCAL_PLAYER_ID)"
        "packetBridgeForceGameLocalPlayerIDStartFrame=$($env:MELONDS_NSML_PACKET_BRIDGE_FORCE_GAME_LOCAL_PLAYER_ID_START_FRAME)"
        "packetBridgeForceGameLocalPlayerIDEarly=$($env:MELONDS_NSML_PACKET_BRIDGE_FORCE_GAME_LOCAL_PLAYER_ID_EARLY)"
        "packetBridgeLocalInputDelay=$($env:MELONDS_NSML_PACKET_BRIDGE_LOCAL_INPUT_DELAY)"
        "packetBridgeNeutralizeLocalInput=$($env:MELONDS_NSML_PACKET_BRIDGE_NEUTRALIZE_LOCAL_INPUT)"
        "packetBridgeSendDelayFrames=$($env:MELONDS_NSML_PACKET_BRIDGE_SEND_DELAY_FRAMES)"
        "packetBridgeSendJitterFrames=$($env:MELONDS_NSML_PACKET_BRIDGE_SEND_JITTER_FRAMES)"
        "disableFrameLimit=$($env:MELONDS_NSML_DISABLE_FRAME_LIMIT)"
        "fixedFrameTime=$($env:MELONDS_NSML_FIXED_FRAME_TIMESTEP)"
        "targetFps=$($env:MELONDS_NSML_TARGET_FPS)"
        "disableHash=$($env:MELONDS_NSML_DISABLE_HASH)"
        "packetBridgeLiveFallbackWindow=$($env:MELONDS_NSML_PACKET_REPLAY_LIVE_FALLBACK_WINDOW)"
        "packetBridgeLiveFallbackNearest=$($env:MELONDS_NSML_PACKET_REPLAY_LIVE_FALLBACK_NEAREST)"
        "packetBridgeLiveFallbackLatestBefore=$($env:MELONDS_NSML_PACKET_REPLAY_LIVE_FALLBACK_LATEST_BEFORE)"
        "packetBridgeLiveFallbackStartFrame=$($env:MELONDS_NSML_PACKET_REPLAY_LIVE_FALLBACK_START_FRAME)"
        "packetBridgeWaitStartFrame=$($env:MELONDS_NSML_PACKET_BRIDGE_WAIT_START_FRAME)"
        "packetBridgeMaintainPacketFreeBytes=$($env:MELONDS_NSML_PACKET_BRIDGE_MAINTAIN_PACKET_FREE_BYTES)"
        "packetBridgeMaintainSessionPeers=$($env:MELONDS_NSML_PACKET_BRIDGE_MAINTAIN_SESSION_PEERS)"
        "packetBridgeMaintainSessionPeersStart=$($env:MELONDS_NSML_PACKET_BRIDGE_MAINTAIN_SESSION_PEERS_START_FRAME)"
        "packetBridgeMaintainSessionPeersHostOnly=$($env:MELONDS_NSML_PACKET_BRIDGE_MAINTAIN_SESSION_PEERS_HOST_ONLY)"
        "packetBridgeMaintainSessionPeersClientOnly=$($env:MELONDS_NSML_PACKET_BRIDGE_MAINTAIN_SESSION_PEERS_CLIENT_ONLY)"
        "packetBridgeLowerStatusResult=$($env:MELONDS_NSML_PACKET_BRIDGE_LOWER_STATUS_RESULT)"
        "packetBridgeClientConfirmToStageStart=$($env:MELONDS_NSML_PACKET_BRIDGE_CLIENT_CONFIRM_TO_STAGE_START)"
        "packetBridgeClientConfirmToStageStartFrame=$($env:MELONDS_NSML_PACKET_BRIDGE_CLIENT_CONFIRM_TO_STAGE_START_FRAME)"
        "packetBridgeStageStartReadyProbe=$($env:MELONDS_NSML_PACKET_BRIDGE_STAGE_START_READY_PROBE)"
        "packetBridgeStageStartPacketAction=$($env:MELONDS_NSML_PACKET_BRIDGE_STAGE_START_PACKET_ACTION)"
        "packetBridgeStageStartNet14=$($env:MELONDS_NSML_PACKET_BRIDGE_STAGE_START_NET14)"
        "packetBridgeStageStartNet1C=$($env:MELONDS_NSML_PACKET_BRIDGE_STAGE_START_NET1C)"
        "packetBridgeStageStartNet20=$($env:MELONDS_NSML_PACKET_BRIDGE_STAGE_START_NET20)"
        "packetBridgeStageStartNet20Step3=$($env:MELONDS_NSML_PACKET_BRIDGE_STAGE_START_NET20_STEP3)"
        "packetBridgeStageStartNet20Step3MinTimer=$($env:MELONDS_NSML_PACKET_BRIDGE_STAGE_START_NET20_STEP3_MIN_TIMER)"
        "packetBridgeStageStartNet20Check=$($env:MELONDS_NSML_PACKET_BRIDGE_STAGE_START_NET20_CHECK)"
        "packetBridgeStageStartNet20CheckMinTimer=$($env:MELONDS_NSML_PACKET_BRIDGE_STAGE_START_NET20_CHECK_MIN_TIMER)"
        "packetBridgeStageStartStep6Close=$($env:MELONDS_NSML_PACKET_BRIDGE_STAGE_START_STEP6_CLOSE)"
        "packetBridgeStageStartStep6CloseMinTimer=$($env:MELONDS_NSML_PACKET_BRIDGE_STAGE_START_STEP6_CLOSE_MIN_TIMER)"
        "packetBridgeStageSceneReadyClose=$($env:MELONDS_NSML_PACKET_BRIDGE_STAGE_SCENE_READY_CLOSE)"
        "packetBridgeStageSceneReadyCloseStartFrame=$($env:MELONDS_NSML_PACKET_BRIDGE_STAGE_SCENE_READY_CLOSE_START_FRAME)"
        "packetBridgeReadPacketByte=$($env:MELONDS_NSML_PACKET_BRIDGE_READ_PACKET_BYTE)"
        "packetBridgeCheckPacketBits=$($env:MELONDS_NSML_PACKET_BRIDGE_CHECK_PACKET_BITS)"
        "packetBridgeForceStageNet20OnStageScene=$($env:MELONDS_NSML_PACKET_BRIDGE_FORCE_STAGE_NET20_ON_STAGE_SCENE)"
        "packetBridgeStageStartNet24=$($env:MELONDS_NSML_PACKET_BRIDGE_STAGE_START_NET24)"
        "packetBridgeStageStartNet2C=$($env:MELONDS_NSML_PACKET_BRIDGE_STAGE_START_NET2C)"
        "packetBridgeStageStartNet34=$($env:MELONDS_NSML_PACKET_BRIDGE_STAGE_START_NET34)"
        "packetBridgeRunStageStartSMUpdate=$($env:MELONDS_NSML_PACKET_BRIDGE_RUN_STAGE_START_SM_UPDATE)"
        "packetBridgeRunStageStartSMUpdateStart=$($env:MELONDS_NSML_PACKET_BRIDGE_RUN_STAGE_START_SM_UPDATE_START_FRAME)"
        "packetBridgeRunVSConnectOnUpdate=$($env:MELONDS_NSML_PACKET_BRIDGE_RUN_VSCONNECT_ON_UPDATE)"
        "packetBridgeRunVSConnectOnUpdateStart=$($env:MELONDS_NSML_PACKET_BRIDGE_RUN_VSCONNECT_ON_UPDATE_START_FRAME)"
        "stageStartDispatchTrace=$($env:MELONDS_NSML_STAGE_START_DISPATCH_TRACE)"
        "stageStartDispatchTraceFull=$($env:MELONDS_NSML_STAGE_START_DISPATCH_TRACE_FULL)"
        "stageStartDispatchTraceLog=$($env:MELONDS_NSML_STAGE_START_DISPATCH_TRACE_LOG)"
        "safeLoadLevel=$($env:MELONDS_NSML_SAFE_LOAD_LEVEL_CALL)"
        "safeLoadLevelPlayerID=$($env:MELONDS_NSML_SAFE_LOAD_LEVEL_PLAYER_ID)"
        "safeLoadLevelSessionReady=$($env:MELONDS_NSML_SAFE_LOAD_LEVEL_SESSION_READY)"
        "safeLoadLevelFilesReady=$($env:MELONDS_NSML_SAFE_LOAD_LEVEL_FILES_READY)"
        "safeStageSceneFactory=$($env:MELONDS_NSML_SAFE_STAGE_SCENE_FACTORY_CALL)"
        "safeStageSceneFactoryCreateObject=$($env:MELONDS_NSML_SAFE_STAGE_SCENE_FACTORY_CREATE_OBJECT)"
        "safeStageSceneFactorySceneSwitch=$($env:MELONDS_NSML_SAFE_STAGE_SCENE_FACTORY_SCENE_SWITCH)"
        "safeStageSceneFactoryInactive=$($env:MELONDS_NSML_SAFE_STAGE_SCENE_FACTORY_INACTIVE)"
        "safeMvlLoadThread=$($env:MELONDS_NSML_SAFE_MVL_LOAD_THREAD_CALL)"
        "safeMvlLoadThreadFrame=$($env:MELONDS_NSML_SAFE_MVL_LOAD_THREAD_CALL_FRAME)"
        "safeMvlLoadThreadPC=$($env:MELONDS_NSML_SAFE_MVL_LOAD_THREAD_CALL_PC)"
        "safeMvlLoadThreadMinSP=$($env:MELONDS_NSML_SAFE_MVL_LOAD_THREAD_CALL_MIN_SP)"
        "safeMvlLoadThreadMode=$($env:MELONDS_NSML_SAFE_MVL_LOAD_THREAD_CALL_MODE)"
        "forceLoadSwitch=$PacketBridgeForceLoadGameSM"
        "forceLoadEnv=$($env:MELONDS_NSML_PACKET_BRIDGE_FORCE_LOAD_GAME_SM)"
        "forceLoadFrame=$($env:MELONDS_NSML_PACKET_BRIDGE_FORCE_LOAD_GAME_SM_START_FRAME)"
        "forceLoadStep=$($env:MELONDS_NSML_PACKET_BRIDGE_FORCE_LOAD_GAME_SM_STEP)"
        "forceMvlFileCache=$($env:MELONDS_NSML_PACKET_BRIDGE_FORCE_MVL_FILE_CACHE)"
        "forceMvlFileCacheStart=$($env:MELONDS_NSML_PACKET_BRIDGE_FORCE_MVL_FILE_CACHE_START_FRAME)"
        "forceMvlLoadThreadSwitch=$PacketBridgeForceMvlLoadThread"
        "forceMvlLoadThreadHostOnly=$PacketBridgeForceMvlLoadThreadHostOnly"
        "forceMvlLoadThreadClientOnly=$PacketBridgeForceMvlLoadThreadClientOnly"
        "forceMvlLoadThread=$($env:MELONDS_NSML_PACKET_BRIDGE_FORCE_MVL_LOAD_THREAD)"
        "forceMvlLoadThreadStart=$($env:MELONDS_NSML_PACKET_BRIDGE_FORCE_MVL_LOAD_THREAD_START_FRAME)"
        "memPatchFile=$($env:MELONDS_NSML_MEM_PATCH_FILE)"
        "memPatchFrame=$($env:MELONDS_NSML_MEM_PATCH_FRAME)"
        "memPatchRanges=$($env:MELONDS_NSML_MEM_PATCH_RANGES)"
        "vsStarSnapFrame=$($env:MELONDS_NSML_VS_STAR_SNAP_FRAME)"
        "vsStarSnapPlayerSlot=$($env:MELONDS_NSML_VS_STAR_SNAP_PLAYER_SLOT)"
        "playerSnapToStarFrame=$($env:MELONDS_NSML_PLAYER_SNAP_TO_STAR_FRAME)"
        "playerSnapToStarSlot=$($env:MELONDS_NSML_PLAYER_SNAP_TO_STAR_SLOT)"
        "playerStickToStarStartFrame=$($env:MELONDS_NSML_PLAYER_STICK_TO_STAR_START_FRAME)"
        "playerStickToStarEndFrame=$($env:MELONDS_NSML_PLAYER_STICK_TO_STAR_END_FRAME)"
        "playerStickToStarSlot=$($env:MELONDS_NSML_PLAYER_STICK_TO_STAR_SLOT)"
        "forceStageCameraSlotSwitch=$ForceStageCameraSlot"
        "forceStageCameraSlotEnv=$($env:MELONDS_NSML_FORCE_STAGE_CAMERA_SLOT)"
        "forceStageCameraSlotStart=$($env:MELONDS_NSML_FORCE_STAGE_CAMERA_SLOT_START_FRAME)"
        "forceStageCameraSlotEnd=$($env:MELONDS_NSML_FORCE_STAGE_CAMERA_SLOT_END_FRAME)"
        "forceStageCameraSlotSource=$($env:MELONDS_NSML_FORCE_STAGE_CAMERA_SLOT_SOURCE)"
        "forceStageCameraSlotDest=$($env:MELONDS_NSML_FORCE_STAGE_CAMERA_SLOT_DEST)"
        "forceStageCameraObjectXSwitch=$ForceStageCameraObjectX"
        "forceStageCameraObjectXEnv=$($env:MELONDS_NSML_FORCE_STAGE_CAMERA_OBJECT_X)"
        "forceStageCameraObjectXStart=$($env:MELONDS_NSML_FORCE_STAGE_CAMERA_OBJECT_X_START_FRAME)"
        "forceStageCameraObjectXEnd=$($env:MELONDS_NSML_FORCE_STAGE_CAMERA_OBJECT_X_END_FRAME)"
        "forceStageCameraObjectXValue=$($env:MELONDS_NSML_FORCE_STAGE_CAMERA_OBJECT_X_VALUE)"
        "forceStageCameraObjectZValue=$($env:MELONDS_NSML_FORCE_STAGE_CAMERA_OBJECT_Z_VALUE)"
        "forceStageCameraObjectXWriteDisplay=$($env:MELONDS_NSML_FORCE_STAGE_CAMERA_OBJECT_X_WRITE_DISPLAY)"
        "forceStageCameraObjectXWriteSlot=$($env:MELONDS_NSML_FORCE_STAGE_CAMERA_OBJECT_X_WRITE_SLOT)"
        "forceStageCameraObjectXSlot=$($env:MELONDS_NSML_FORCE_STAGE_CAMERA_OBJECT_X_SLOT)"
        "forcePlayerInventoryPowerupsSwitch=$ForcePlayerInventoryPowerups"
        "forcePlayerInventoryPowerupsEnv=$($env:MELONDS_NSML_FORCE_PLAYER_INVENTORY_POWERUPS)"
        "forcePlayerInventoryPowerupsStart=$($env:MELONDS_NSML_FORCE_PLAYER_INVENTORY_POWERUPS_START_FRAME)"
        "forcePlayerInventoryPowerupsEnd=$($env:MELONDS_NSML_FORCE_PLAYER_INVENTORY_POWERUPS_END_FRAME)"
        "forcePlayerInventoryPowerup0=$($env:MELONDS_NSML_FORCE_PLAYER_INVENTORY_POWERUP0)"
        "forcePlayerInventoryPowerup1=$($env:MELONDS_NSML_FORCE_PLAYER_INVENTORY_POWERUP1)"
        "forcePlayerStarCountersSwitch=$ForcePlayerStarCounters"
        "forcePlayerStarCountersEnv=$($env:MELONDS_NSML_FORCE_PLAYER_STAR_COUNTERS)"
        "forcePlayerStarCountersStart=$($env:MELONDS_NSML_FORCE_PLAYER_STAR_COUNTERS_START_FRAME)"
        "forcePlayerStarCountersEnd=$($env:MELONDS_NSML_FORCE_PLAYER_STAR_COUNTERS_END_FRAME)"
        "forcePlayerBattleStars0=$($env:MELONDS_NSML_FORCE_PLAYER_BATTLE_STARS0)"
        "forcePlayerBattleStars1=$($env:MELONDS_NSML_FORCE_PLAYER_BATTLE_STARS1)"
        "forcePlayerDisplayedStars0=$($env:MELONDS_NSML_FORCE_PLAYER_DISPLAYED_STARS0)"
        "forcePlayerDisplayedStars1=$($env:MELONDS_NSML_FORCE_PLAYER_DISPLAYED_STARS1)"
        "forcePlayerCollectedStars0=$($env:MELONDS_NSML_FORCE_PLAYER_COLLECTED_STARS0)"
        "forcePlayerCollectedStars1=$($env:MELONDS_NSML_FORCE_PLAYER_COLLECTED_STARS1)"
        "tracePlayerLifeChangesSwitch=$TracePlayerLifeChanges"
        "tracePlayerLifeChangesEnv=$($env:MELONDS_NSML_TRACE_PLAYER_LIFE_CHANGES)"
        "tracePlayerLifeCallsSwitch=$TracePlayerLifeCalls"
        "tracePlayerLifeCallsEnv=$($env:MELONDS_NSML_TRACE_PLAYER_LIFE_CALLS)"
        "tracePlayerDefeatedSwitch=$TracePlayerDefeated"
        "tracePlayerDefeatedEnv=$($env:MELONDS_NSML_TRACE_PLAYER_DEFEATED)"
        "safeCreateSwitch=$PacketBridgeSafeCreateLoadGameSM"
        "safeCreateEnv=$($env:MELONDS_NSML_SAFE_CREATE_LOAD_GAME_CALL)"
        "safeCreateFrame=$($env:MELONDS_NSML_SAFE_CREATE_LOAD_GAME_CALL_FRAME)"
        "safeCreatePC=$($env:MELONDS_NSML_SAFE_CREATE_LOAD_GAME_CALL_PC)"
        "scheduleSwitch=$PacketBridgeScheduleLoadGameSM"
        "scheduleEnv=$($env:MELONDS_NSML_FORCE_SCHEDULE_LOAD_GAME_SM_ARGS)"
        "scheduleFrame=$($env:MELONDS_NSML_FORCE_SCHEDULE_LOAD_GAME_SM_ARGS_FRAME)"
        "submenuSchedule=$($env:MELONDS_NSML_PACKET_BRIDGE_SUBMENU_SCHEDULE)"
        "submenuDirect=$($env:MELONDS_NSML_PACKET_BRIDGE_SUBMENU_DIRECT)"
        "submenuCallCreate=$($env:MELONDS_NSML_PACKET_BRIDGE_SUBMENU_CALL_CREATE)"
        "forceStageStartSMFields=$($env:MELONDS_NSML_PACKET_BRIDGE_FORCE_STAGE_START_SM_FIELDS)"
        "forceStageStartSMFieldsStart=$($env:MELONDS_NSML_PACKET_BRIDGE_FORCE_STAGE_START_SM_FIELDS_START_FRAME)"
        "packetBridgeThrottleStartFrame=$($env:MELONDS_NSML_PACKET_BRIDGE_THROTTLE_START_FRAME)"
    ) |
        Set-Content -Encoding UTF8 "$Stdout.env.txt"
    $err = "$Stdout.err"
    $process = Start-Process -FilePath $exePath `
        -ArgumentList "`"$RoleRom`"" `
        -WorkingDirectory $logRoot `
        -RedirectStandardOutput $Stdout `
        -RedirectStandardError $err `
        -PassThru
    return [pscustomobject]@{
        Process = $process
        Stdout = $Stdout
        Stderr = $err
    }
}

function Wait-LogPattern {
    param(
        [string]$Path,
        [string]$Pattern,
        [int]$TimeoutMs
    )

    $deadline = [DateTime]::UtcNow.AddMilliseconds($TimeoutMs)
    while ([DateTime]::UtcNow -lt $deadline) {
        if ((Test-Path $Path) -and (Select-String -Path $Path -Pattern $Pattern -Quiet)) {
            return
        }
        Start-Sleep -Milliseconds 100
    }
    throw "timed out waiting for '$Pattern' in $Path"
}

function Complete-MelonLANProcess {
    param($Started)

    $process = $Started.Process
    if (-not $process.WaitForExit($WaitTimeoutMs)) {
        $process.Kill()
        throw "melonDS process timed out. pid=$($process.Id)"
    }
    $process.Refresh()

    if (Test-Path $Started.Stderr) {
        Add-Content -Path $Started.Stdout -Value (Get-Content $Started.Stderr -Raw)
    }

    if ($null -ne $process.ExitCode -and $process.ExitCode -ne 0) {
        throw "melonDS exited with code $($process.ExitCode). See $($Started.Stdout)"
    }
}

$hostProc = $null
$clientProc = $null
try {
    if ($RunRole -eq "both" -or $RunRole -eq "host") {
        $hostProc = Start-MelonLANProcess -Role "host" -RoleRom $hostRom -RoleInput $hostInput -Stdout $hostOut -HashLog $hostHash -ScreenshotDir $hostScreens -GameStateTracePath $hostGameStateTrace -LanMPTracePath $hostLanMPTrace -PacketReplayFile $HostPacketReplayFile -PacketCapturePath $hostPacketCapture -RamDumpDir $hostRamDumps
    }
    if ($RunRole -eq "both") {
        Start-Sleep -Milliseconds $HostStartupDelayMs
    }
    if ($RunRole -eq "both" -or $RunRole -eq "client") {
        $clientProc = Start-MelonLANProcess -Role "client" -RoleRom $clientRom -RoleInput $clientInput -Stdout $clientOut -HashLog $clientHash -ScreenshotDir $clientScreens -GameStateTracePath $clientGameStateTrace -LanMPTracePath $clientLanMPTrace -PacketReplayFile $ClientPacketReplayFile -PacketCapturePath $clientPacketCapture -RamDumpDir $clientRamDumps
    }

    if ($clientProc) {
        Complete-MelonLANProcess $clientProc
    }
    if ($hostProc) {
        Complete-MelonLANProcess $hostProc
    }
} catch {
    foreach ($started in @($hostProc, $clientProc)) {
        if ($null -ne $started -and $null -ne $started.Process -and -not $started.Process.HasExited) {
            $started.Process.Kill()
        }
    }
    throw
}

$roleInfos = @()
function Convert-ExpectedLocalPlayerID {
    param(
        [string]$Value,
        [string]$Default
    )
    if ([string]::IsNullOrWhiteSpace($Value)) {
        return $Default
    }
    $text = $Value.Trim()
    if ($text.StartsWith("0x", [System.StringComparison]::OrdinalIgnoreCase)) {
        return ("0x{0:x}" -f [Convert]::ToInt32($text.Substring(2), 16))
    }
    return ("0x{0:x}" -f [Convert]::ToInt32($text, 10))
}
$expectedHostLocalPlayerID = Convert-ExpectedLocalPlayerID -Value $HostPacketBridgeForceGameLocalPlayerID -Default "0x0"
$expectedClientLocalPlayerID = Convert-ExpectedLocalPlayerID -Value $ClientPacketBridgeForceGameLocalPlayerID -Default "0x1"
if ($RunRole -eq "both" -or $RunRole -eq "host") {
    $roleInfos += @{
        Role = "host"
        Out = $hostOut
        Hash = $hostHash
        Screens = $hostScreens
        GameState = $hostGameStateTrace
        LanStartPattern = "LAN host start .* ok=1"
        LanStartName = "host LAN start"
        LocalPlayerID = $expectedHostLocalPlayerID
    }
}
if ($RunRole -eq "both" -or $RunRole -eq "client") {
    $roleInfos += @{
        Role = "client"
        Out = $clientOut
        Hash = $clientHash
        Screens = $clientScreens
        GameState = $clientGameStateTrace
        LanStartPattern = "LAN client start .* ok=1"
        LanStartName = "client LAN start"
        LocalPlayerID = $expectedClientLocalPlayerID
    }
}

$requiredPatterns = @()
foreach ($info in $roleInfos) {
    $requiredPatterns += @{ Path = $info.Out; Pattern = "frame limit reached"; Name = "$($info.Role) frame limit" }
}
if (-not $NoLanMP -and -not ($RunRole -ne "both" -and $ScriptRemotePacket -and $PacketBridgeArmOnly)) {
    foreach ($info in $roleInfos) {
        $requiredPatterns = @(@{ Path = $info.Out; Pattern = $info.LanStartPattern; Name = $info.LanStartName }) + $requiredPatterns
    }
}

foreach ($item in $requiredPatterns) {
    if (-not (Select-String -Path $item.Path -Pattern $item.Pattern -Quiet)) {
        throw "missing $($item.Name). See $($item.Path)"
    }
}

if (-not $NoHashLog) {
    foreach ($hashLog in @($roleInfos | ForEach-Object { $_.Hash })) {
        if (-not (Test-Path $hashLog)) {
            throw "hash log was not created: $hashLog"
        }
        $rows = Import-Csv $hashLog
        if (-not ($rows | Where-Object { $_.instance -eq "0" })) {
            throw "hash log did not contain instance 0 rows: $hashLog"
        }
    }
}

if ($ScreenshotInterval -gt 0) {
    foreach ($screenDir in @($roleInfos | ForEach-Object { $_.Screens })) {
        $screens = Get-ChildItem $screenDir -Filter "inst0_*.png" -ErrorAction SilentlyContinue
        if (-not $screens) {
            throw "expected screenshots in $screenDir"
        }
    }
}

function Test-DisconnectLikeScreenshot {
    param([string]$Path)

    Add-Type -AssemblyName System.Drawing
    $bitmap = [System.Drawing.Bitmap]::FromFile($Path)
    try {
        $sampleCount = 0
        $blackCount = 0
        $brightCount = 0
        for ($y = 0; $y -lt $bitmap.Height; $y += 4) {
            for ($x = 0; $x -lt $bitmap.Width; $x += 4) {
                $pixel = $bitmap.GetPixel($x, $y)
                $sampleCount++
                if ($pixel.R -lt 18 -and $pixel.G -lt 18 -and $pixel.B -lt 18) {
                    $blackCount++
                } elseif ($pixel.R -gt 150 -and $pixel.G -gt 150 -and $pixel.B -gt 150) {
                    $brightCount++
                }
            }
        }

        if ($sampleCount -eq 0) {
            return $false
        }

        $blackRatio = $blackCount / $sampleCount
        $brightRatio = $brightCount / $sampleCount
        return ($blackRatio -gt 0.82 -and $brightRatio -gt 0.004 -and $brightRatio -lt 0.20)
    } finally {
        $bitmap.Dispose()
    }
}

function Test-BlankLikeScreenshot {
    param([string]$Path)

    Add-Type -AssemblyName System.Drawing
    $bitmap = [System.Drawing.Bitmap]::FromFile($Path)
    try {
        $sampleCount = 0
        $blackCount = 0
        $brightCount = 0
        for ($y = 0; $y -lt $bitmap.Height; $y += 4) {
            for ($x = 0; $x -lt $bitmap.Width; $x += 4) {
                $pixel = $bitmap.GetPixel($x, $y)
                $sampleCount++
                if ($pixel.R -lt 18 -and $pixel.G -lt 18 -and $pixel.B -lt 18) {
                    $blackCount++
                } elseif ($pixel.R -gt 150 -and $pixel.G -gt 150 -and $pixel.B -gt 150) {
                    $brightCount++
                }
            }
        }

        if ($sampleCount -eq 0) {
            return $false
        }

        $blackRatio = $blackCount / $sampleCount
        $brightRatio = $brightCount / $sampleCount
        return ($blackRatio -gt 0.995 -and $brightRatio -lt 0.001)
    } finally {
        $bitmap.Dispose()
    }
}

function Test-ConnectionDialogScreenshot {
    param([string]$Path)

    Add-Type -AssemblyName System.Drawing
    $bitmap = [System.Drawing.Bitmap]::FromFile($Path)
    try {
        $sampleCount = 0
        $blackCount = 0
        $brightCount = 0
        $x0 = [Math]::Max(0, [int]($bitmap.Width * 0.08))
        $x1 = [Math]::Min($bitmap.Width - 1, [int]($bitmap.Width * 0.92))
        $y0 = [Math]::Max(0, [int]($bitmap.Height * 0.11))
        $y1 = [Math]::Min($bitmap.Height - 1, [int]($bitmap.Height * 0.43))
        for ($y = $y0; $y -le $y1; $y += 4) {
            for ($x = $x0; $x -le $x1; $x += 4) {
                $pixel = $bitmap.GetPixel($x, $y)
                $sampleCount++
                if ($pixel.R -lt 22 -and $pixel.G -lt 22 -and $pixel.B -lt 22) {
                    $blackCount++
                } elseif ($pixel.R -gt 170 -and $pixel.G -gt 170 -and $pixel.B -gt 170) {
                    $brightCount++
                }
            }
        }

        if ($sampleCount -eq 0) {
            return $false
        }

        $blackRatio = $blackCount / $sampleCount
        $brightRatio = $brightCount / $sampleCount
        return ($blackRatio -gt 0.55 -and $brightRatio -gt 0.01)
    } finally {
        $bitmap.Dispose()
    }
}

function Convert-TraceHexToInt64 {
    param([string]$Value)

    if ([string]::IsNullOrWhiteSpace($Value)) {
        return 0
    }

    $text = $Value.Trim()
    if ($text.StartsWith("0x", [StringComparison]::OrdinalIgnoreCase)) {
        return [Convert]::ToInt64($text.Substring(2), 16)
    }

    return [Convert]::ToInt64($text, 10)
}

if ($ScreenshotInterval -gt 0) {
    foreach ($screenDir in @($roleInfos | ForEach-Object { $_.Screens })) {
        $screens = Get-ChildItem $screenDir -Filter "inst0_frame*.png" -ErrorAction SilentlyContinue
        foreach ($screen in $screens) {
            if ($screen.Name -notmatch "frame(\d+)\.png") {
                continue
            }

            $frame = [int]$matches[1]
            if ($frame -lt 3000) {
                continue
            }

            if (-not $SkipDisconnectScreenshotCheck) {
                if (Test-DisconnectLikeScreenshot -Path $screen.FullName) {
                    throw "disconnect-like screenshot detected at frame=${frame}: $($screen.FullName)"
                }
                if (Test-ConnectionDialogScreenshot -Path $screen.FullName) {
                    throw "connection-dialog screenshot detected at frame=${frame}: $($screen.FullName)"
                }
            }

            if (-not $SkipBlankScreenshotCheck -and (Test-BlankLikeScreenshot -Path $screen.FullName)) {
                throw "blank-like screenshot detected at frame=${frame}: $($screen.FullName)"
            }
        }
    }
}

if (-not $SkipArmAbortCheck) {
    foreach ($item in @($roleInfos | ForEach-Object { @{ Path = $_.Out; Role = $_.Role } })) {
        if (-not (Test-Path $item.Path)) {
            continue
        }

        $abort = Select-String -Path $item.Path -Pattern "ARM[79]: data abort|ARM[79]: prefetch abort" -CaseSensitive:$false | Select-Object -First 1
        if ($abort) {
            throw "ARM abort detected for $($item.Role): $($abort.Line.Trim()). See $($item.Path)"
        }
    }
}

if ($GameStateTrace -and -not $SkipMvlStateCheck -and ($GameStateTraceEndFrame -le 0 -or $GameStateTraceEndFrame -ge $Frames)) {
    foreach ($item in @($roleInfos | ForEach-Object { @{ Path = $_.GameState; Role = $_.Role; LocalPlayerID = $_.LocalPlayerID } })) {
        if (-not (Test-Path $item.Path)) {
            throw "game state trace was not created for $($item.Role): $($item.Path)"
        }

        $last = Import-Csv $item.Path | Select-Object -Last 1
        if (-not $last) {
            throw "game state trace is empty for $($item.Role): $($item.Path)"
        }

        if ($last.stageGroup -ne "0x9" -or $last.vsMode -ne "0x1" -or $last.localPlayerID -ne $item.LocalPlayerID) {
            throw "Mario vs Luigi state check failed for $($item.Role): stageGroup=$($last.stageGroup) vsMode=$($last.vsMode) localPlayerID=$($last.localPlayerID). See $($item.Path)"
        }

        if (-not $SkipGameplayActorCheck) {
            if ($last.playerActor0Found -ne "0x1" -or $last.playerActor1Found -ne "0x1" -or $last.vsStarActorFound -ne "0x1") {
                throw "Mario vs Luigi gameplay actor check failed for $($item.Role): playerActor0=$($last.playerActor0Found) playerActor1=$($last.playerActor1Found) vsStarActor=$($last.vsStarActorFound). See $($item.Path)"
            }
        }
    }

    if ($RequireClientRemotePlayer0Movement) {
        if (-not (Test-Path $clientGameStateTrace)) {
            throw "client game state trace was not created: $clientGameStateTrace"
        }

        $clientRows = @(Import-Csv $clientGameStateTrace)
        if ($clientRows.Count -eq 0) {
            throw "client game state trace is empty: $clientGameStateTrace"
        }

        $movementStartFrame = $PacketBridgeLiveFallbackStartFrame
        if ($movementStartFrame -le 0) {
            $movementStartFrame = $GameStateTraceStartFrame
        }

        $candidateRows = @($clientRows | Where-Object { [int]$_.frame -ge $movementStartFrame })
        if ($candidateRows.Count -eq 0) {
            throw "client remote movement check has no rows at or after frame $movementStartFrame. See $clientGameStateTrace"
        }

        $first = $candidateRows[0]
        $firstX = Convert-TraceHexToInt64 $first.playerActor0X
        $inputRows = @($candidateRows | Where-Object { (Convert-TraceHexToInt64 $_.inputPlayer0Held) -ne 0 })
        $movedRows = @($candidateRows | Where-Object { (Convert-TraceHexToInt64 $_.playerActor0X) -ne $firstX })

        if ($inputRows.Count -eq 0 -or $movedRows.Count -eq 0) {
            $last = $candidateRows[-1]
            throw "client remote player0 movement check failed: rows=$($candidateRows.Count) inputRows=$($inputRows.Count) movedRows=$($movedRows.Count) firstX=$($first.playerActor0X) lastX=$($last.playerActor0X) lastInput=$($last.inputPlayer0Held). See $clientGameStateTrace"
        }
    }
}

Write-Host "NSMB Mario vs Luigi LAN route smoke passed: frames=$Frames"
