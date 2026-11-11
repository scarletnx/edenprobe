// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Made by features.py from the Vulkan registry 1.4.357: `make features` makes it again.
// Read by masquerade.c and check_masquerade.c through features.h, nothing else.

#define MASQUERADE_REGISTRY "1.4.357"

static const char *const MEMBERS[] = {
    // VkPhysicalDeviceFeatures
    "robustBufferAccess",
    "fullDrawIndexUint32",
    "imageCubeArray",
    "independentBlend",
    "geometryShader",
    "tessellationShader",
    "sampleRateShading",
    "dualSrcBlend",
    "logicOp",
    "multiDrawIndirect",
    "drawIndirectFirstInstance",
    "depthClamp",
    "depthBiasClamp",
    "fillModeNonSolid",
    "depthBounds",
    "wideLines",
    "largePoints",
    "alphaToOne",
    "multiViewport",
    "samplerAnisotropy",
    "textureCompressionETC2",
    "textureCompressionASTC_LDR",
    "textureCompressionBC",
    "occlusionQueryPrecise",
    "pipelineStatisticsQuery",
    "vertexPipelineStoresAndAtomics",
    "fragmentStoresAndAtomics",
    "shaderTessellationAndGeometryPointSize",
    "shaderImageGatherExtended",
    "shaderStorageImageExtendedFormats",
    "shaderStorageImageMultisample",
    "shaderStorageImageReadWithoutFormat",
    "shaderStorageImageWriteWithoutFormat",
    "shaderUniformBufferArrayDynamicIndexing",
    "shaderSampledImageArrayDynamicIndexing",
    "shaderStorageBufferArrayDynamicIndexing",
    "shaderStorageImageArrayDynamicIndexing",
    "shaderClipDistance",
    "shaderCullDistance",
    "shaderFloat64",
    "shaderInt64",
    "shaderInt16",
    "shaderResourceResidency",
    "shaderResourceMinLod",
    "sparseBinding",
    "sparseResidencyBuffer",
    "sparseResidencyImage2D",
    "sparseResidencyImage3D",
    "sparseResidency2Samples",
    "sparseResidency4Samples",
    "sparseResidency8Samples",
    "sparseResidency16Samples",
    "sparseResidencyAliased",
    "variableMultisampleRate",
    "inheritedQueries",
    // VkPhysicalDeviceVulkan11Features
    "storageBuffer16BitAccess",
    "uniformAndStorageBuffer16BitAccess",
    "storagePushConstant16",
    "storageInputOutput16",
    "multiview",
    "multiviewGeometryShader",
    "multiviewTessellationShader",
    "variablePointersStorageBuffer",
    "variablePointers",
    "protectedMemory",
    "samplerYcbcrConversion",
    "shaderDrawParameters",
    // VkPhysicalDeviceVulkan12Features
    "samplerMirrorClampToEdge",
    "drawIndirectCount",
    "storageBuffer8BitAccess",
    "uniformAndStorageBuffer8BitAccess",
    "storagePushConstant8",
    "shaderBufferInt64Atomics",
    "shaderSharedInt64Atomics",
    "shaderFloat16",
    "shaderInt8",
    "descriptorIndexing",
    "shaderInputAttachmentArrayDynamicIndexing",
    "shaderUniformTexelBufferArrayDynamicIndexing",
    "shaderStorageTexelBufferArrayDynamicIndexing",
    "shaderUniformBufferArrayNonUniformIndexing",
    "shaderSampledImageArrayNonUniformIndexing",
    "shaderStorageBufferArrayNonUniformIndexing",
    "shaderStorageImageArrayNonUniformIndexing",
    "shaderInputAttachmentArrayNonUniformIndexing",
    "shaderUniformTexelBufferArrayNonUniformIndexing",
    "shaderStorageTexelBufferArrayNonUniformIndexing",
    "descriptorBindingUniformBufferUpdateAfterBind",
    "descriptorBindingSampledImageUpdateAfterBind",
    "descriptorBindingStorageImageUpdateAfterBind",
    "descriptorBindingStorageBufferUpdateAfterBind",
    "descriptorBindingUniformTexelBufferUpdateAfterBind",
    "descriptorBindingStorageTexelBufferUpdateAfterBind",
    "descriptorBindingUpdateUnusedWhilePending",
    "descriptorBindingPartiallyBound",
    "descriptorBindingVariableDescriptorCount",
    "runtimeDescriptorArray",
    "samplerFilterMinmax",
    "scalarBlockLayout",
    "imagelessFramebuffer",
    "uniformBufferStandardLayout",
    "shaderSubgroupExtendedTypes",
    "separateDepthStencilLayouts",
    "hostQueryReset",
    "timelineSemaphore",
    "bufferDeviceAddress",
    "bufferDeviceAddressCaptureReplay",
    "bufferDeviceAddressMultiDevice",
    "vulkanMemoryModel",
    "vulkanMemoryModelDeviceScope",
    "vulkanMemoryModelAvailabilityVisibilityChains",
    "shaderOutputViewportIndex",
    "shaderOutputLayer",
    "subgroupBroadcastDynamicId",
    // VkPhysicalDeviceVulkan13Features
    "robustImageAccess",
    "inlineUniformBlock",
    "descriptorBindingInlineUniformBlockUpdateAfterBind",
    "pipelineCreationCacheControl",
    "privateData",
    "shaderDemoteToHelperInvocation",
    "shaderTerminateInvocation",
    "subgroupSizeControl",
    "computeFullSubgroups",
    "synchronization2",
    "textureCompressionASTC_HDR",
    "shaderZeroInitializeWorkgroupMemory",
    "dynamicRendering",
    "shaderIntegerDotProduct",
    "maintenance4",
    // VkPhysicalDeviceVulkan14Features
    "globalPriorityQuery",
    "shaderSubgroupRotate",
    "shaderSubgroupRotateClustered",
    "shaderFloatControls2",
    "shaderExpectAssume",
    "rectangularLines",
    "bresenhamLines",
    "smoothLines",
    "stippledRectangularLines",
    "stippledBresenhamLines",
    "stippledSmoothLines",
    "vertexAttributeInstanceRateDivisor",
    "vertexAttributeInstanceRateZeroDivisor",
    "indexTypeUint8",
    "dynamicRenderingLocalRead",
    "maintenance5",
    "maintenance6",
    "pipelineProtectedAccess",
    "pipelineRobustness",
    "hostImageCopy",
    "pushDescriptor",
    // VkPhysicalDeviceTransformFeedbackFeaturesEXT
    "transformFeedback",
    "geometryStreams",
    // VkPhysicalDeviceDynamicRenderingFeatures
    "dynamicRendering",
    // VkPhysicalDeviceCornerSampledImageFeaturesNV
    "cornerSampledImage",
    // VkPhysicalDeviceMultiviewFeatures
    "multiview",
    "multiviewGeometryShader",
    "multiviewTessellationShader",
    // VkPhysicalDeviceShaderDrawParametersFeatures
    "shaderDrawParameters",
    // VkPhysicalDeviceTextureCompressionASTCHDRFeatures
    "textureCompressionASTC_HDR",
    // VkPhysicalDeviceASTCDecodeFeaturesEXT
    "decodeModeSharedExponent",
    // VkPhysicalDevicePipelineRobustnessFeatures
    "pipelineRobustness",
    // VkPhysicalDeviceConditionalRenderingFeaturesEXT
    "conditionalRendering",
    "inheritedConditionalRendering",
    // VkPhysicalDeviceShaderFloat16Int8Features
    "shaderFloat16",
    "shaderInt8",
    // VkPhysicalDevice16BitStorageFeatures
    "storageBuffer16BitAccess",
    "uniformAndStorageBuffer16BitAccess",
    "storagePushConstant16",
    "storageInputOutput16",
    // VkPhysicalDeviceDepthClipEnableFeaturesEXT
    "depthClipEnable",
    // VkPhysicalDeviceImagelessFramebufferFeatures
    "imagelessFramebuffer",
    // VkPhysicalDeviceRelaxedLineRasterizationFeaturesIMG
    "relaxedLineRasterization",
    // VkPhysicalDevicePerformanceQueryFeaturesKHR
    "performanceCounterQueryPools",
    "performanceCounterMultipleQueryPools",
    // VkPhysicalDeviceVariablePointersFeatures
    "variablePointersStorageBuffer",
    "variablePointers",
    // VkPhysicalDeviceGpaFeaturesAMD
    "perfCounters",
    "streamingPerfCounters",
    "sqThreadTracing",
    "clockModes",
    // VkPhysicalDeviceShaderEnqueueFeaturesAMDX
    "shaderEnqueue",
    "shaderMeshEnqueue",
    // VkPhysicalDeviceDescriptorHeapFeaturesEXT
    "descriptorHeap",
    "descriptorHeapCaptureReplay",
    // VkPhysicalDeviceInlineUniformBlockFeatures
    "inlineUniformBlock",
    "descriptorBindingInlineUniformBlockUpdateAfterBind",
    // VkPhysicalDeviceShaderBfloat16FeaturesKHR
    "shaderBFloat16Type",
    "shaderBFloat16DotProduct",
    "shaderBFloat16CooperativeMatrix",
    // VkPhysicalDeviceProtectedMemoryFeatures
    "protectedMemory",
    // VkPhysicalDeviceBlendOperationAdvancedFeaturesEXT
    "advancedBlendCoherentOperations",
    // VkPhysicalDeviceAccelerationStructureFeaturesKHR
    "accelerationStructure",
    "accelerationStructureCaptureReplay",
    "accelerationStructureIndirectBuild",
    "accelerationStructureHostCommands",
    "descriptorBindingAccelerationStructureUpdateAfterBind",
    // VkPhysicalDeviceShaderSMBuiltinsFeaturesNV
    "shaderSMBuiltins",
    // VkPhysicalDeviceSamplerYcbcrConversionFeatures
    "samplerYcbcrConversion",
    // VkPhysicalDeviceDescriptorIndexingFeatures
    "shaderInputAttachmentArrayDynamicIndexing",
    "shaderUniformTexelBufferArrayDynamicIndexing",
    "shaderStorageTexelBufferArrayDynamicIndexing",
    "shaderUniformBufferArrayNonUniformIndexing",
    "shaderSampledImageArrayNonUniformIndexing",
    "shaderStorageBufferArrayNonUniformIndexing",
    "shaderStorageImageArrayNonUniformIndexing",
    "shaderInputAttachmentArrayNonUniformIndexing",
    "shaderUniformTexelBufferArrayNonUniformIndexing",
    "shaderStorageTexelBufferArrayNonUniformIndexing",
    "descriptorBindingUniformBufferUpdateAfterBind",
    "descriptorBindingSampledImageUpdateAfterBind",
    "descriptorBindingStorageImageUpdateAfterBind",
    "descriptorBindingStorageBufferUpdateAfterBind",
    "descriptorBindingUniformTexelBufferUpdateAfterBind",
    "descriptorBindingStorageTexelBufferUpdateAfterBind",
    "descriptorBindingUpdateUnusedWhilePending",
    "descriptorBindingPartiallyBound",
    "descriptorBindingVariableDescriptorCount",
    "runtimeDescriptorArray",
    // VkPhysicalDevicePortabilitySubsetFeaturesKHR
    "constantAlphaColorBlendFactors",
    "events",
    "imageViewFormatReinterpretation",
    "imageViewFormatSwizzle",
    "imageView2DOn3DImage",
    "multisampleArrayImage",
    "mutableComparisonSamplers",
    "pointPolygons",
    "samplerMipLodBias",
    "separateStencilMaskRef",
    "shaderSampleRateInterpolationFunctions",
    "tessellationIsolines",
    "tessellationPointMode",
    "triangleFans",
    "vertexAttributeAccessBeyondStride",
    // VkPhysicalDeviceShadingRateImageFeaturesNV
    "shadingRateImage",
    "shadingRateCoarseSampleOrder",
    // VkPhysicalDeviceRepresentativeFragmentTestFeaturesNV
    "representativeFragmentTest",
    // VkPhysicalDeviceCooperativeMatrixConversionFeaturesQCOM
    "cooperativeMatrixConversion",
    // VkPhysicalDeviceElapsedTimerQueryFeaturesQCOM
    "elapsedTimerQuery",
    // VkPhysicalDeviceShaderSubgroupExtendedTypesFeatures
    "shaderSubgroupExtendedTypes",
    // VkPhysicalDevice8BitStorageFeatures
    "storageBuffer8BitAccess",
    "uniformAndStorageBuffer8BitAccess",
    "storagePushConstant8",
    // VkPhysicalDeviceShaderAtomicInt64Features
    "shaderBufferInt64Atomics",
    "shaderSharedInt64Atomics",
    // VkPhysicalDeviceShaderClockFeaturesKHR
    "shaderSubgroupClock",
    "shaderDeviceClock",
    // VkPhysicalDeviceVertexAttributeDivisorFeatures
    "vertexAttributeInstanceRateDivisor",
    "vertexAttributeInstanceRateZeroDivisor",
    // VkPhysicalDeviceComputeShaderDerivativesFeaturesKHR
    "computeDerivativeGroupQuads",
    "computeDerivativeGroupLinear",
    // VkPhysicalDeviceMeshShaderFeaturesNV
    "taskShader",
    "meshShader",
    // VkPhysicalDeviceFragmentShaderBarycentricFeaturesKHR
    "fragmentShaderBarycentric",
    // VkPhysicalDeviceShaderImageFootprintFeaturesNV
    "imageFootprint",
    // VkPhysicalDeviceExclusiveScissorFeaturesNV
    "exclusiveScissor",
    // VkPhysicalDeviceTimelineSemaphoreFeatures
    "timelineSemaphore",
    // VkPhysicalDevicePresentTimingFeaturesEXT
    "presentTiming",
    "presentAtAbsoluteTime",
    "presentAtRelativeTime",
    // VkPhysicalDeviceShaderIntegerFunctions2FeaturesINTEL
    "shaderIntegerFunctions2",
    // VkPhysicalDeviceVulkanMemoryModelFeatures
    "vulkanMemoryModel",
    "vulkanMemoryModelDeviceScope",
    "vulkanMemoryModelAvailabilityVisibilityChains",
    // VkPhysicalDeviceShaderTerminateInvocationFeatures
    "shaderTerminateInvocation",
    // VkPhysicalDeviceFragmentDensityMapFeaturesEXT
    "fragmentDensityMap",
    "fragmentDensityMapDynamic",
    "fragmentDensityMapNonSubsampledImages",
    // VkPhysicalDeviceScalarBlockLayoutFeatures
    "scalarBlockLayout",
    // VkPhysicalDeviceSubgroupSizeControlFeatures
    "subgroupSizeControl",
    "computeFullSubgroups",
    // VkPhysicalDeviceFragmentShadingRateFeaturesKHR
    "pipelineFragmentShadingRate",
    "primitiveFragmentShadingRate",
    "attachmentFragmentShadingRate",
    // VkPhysicalDeviceCoherentMemoryFeaturesAMD
    "deviceCoherentMemory",
    // VkPhysicalDeviceShaderConstantDataFeaturesKHR
    "shaderConstantData",
    // VkPhysicalDeviceDynamicRenderingLocalReadFeatures
    "dynamicRenderingLocalRead",
    // VkPhysicalDeviceShaderAbortFeaturesKHR
    "shaderAbort",
    // VkPhysicalDeviceShaderImageAtomicInt64FeaturesEXT
    "shaderImageInt64Atomics",
    "sparseImageInt64Atomics",
    // VkPhysicalDeviceShaderQuadControlFeaturesKHR
    "shaderQuadControl",
    // VkPhysicalDeviceMemoryPriorityFeaturesEXT
    "memoryPriority",
    // VkPhysicalDeviceDedicatedAllocationImageAliasingFeaturesNV
    "dedicatedAllocationImageAliasing",
    // VkPhysicalDeviceSeparateDepthStencilLayoutsFeatures
    "separateDepthStencilLayouts",
    // VkPhysicalDeviceBufferDeviceAddressFeaturesEXT
    "bufferDeviceAddress",
    "bufferDeviceAddressCaptureReplay",
    "bufferDeviceAddressMultiDevice",
    // VkPhysicalDevicePresentWaitFeaturesKHR
    "presentWait",
    // VkPhysicalDeviceCooperativeMatrixFeaturesNV
    "cooperativeMatrix",
    "cooperativeMatrixRobustBufferAccess",
    // VkPhysicalDeviceCoverageReductionModeFeaturesNV
    "coverageReductionMode",
    // VkPhysicalDeviceFragmentShaderInterlockFeaturesEXT
    "fragmentShaderSampleInterlock",
    "fragmentShaderPixelInterlock",
    "fragmentShaderShadingRateInterlock",
    // VkPhysicalDeviceYcbcrImageArraysFeaturesEXT
    "ycbcrImageArrays",
    // VkPhysicalDeviceUniformBufferStandardLayoutFeatures
    "uniformBufferStandardLayout",
    // VkPhysicalDeviceProvokingVertexFeaturesEXT
    "provokingVertexLast",
    "transformFeedbackPreservesProvokingVertex",
    // VkPhysicalDeviceBufferDeviceAddressFeatures
    "bufferDeviceAddress",
    "bufferDeviceAddressCaptureReplay",
    "bufferDeviceAddressMultiDevice",
    // VkPhysicalDeviceLineRasterizationFeatures
    "rectangularLines",
    "bresenhamLines",
    "smoothLines",
    "stippledRectangularLines",
    "stippledBresenhamLines",
    "stippledSmoothLines",
    // VkPhysicalDeviceShaderAtomicFloatFeaturesEXT
    "shaderBufferFloat32Atomics",
    "shaderBufferFloat32AtomicAdd",
    "shaderBufferFloat64Atomics",
    "shaderBufferFloat64AtomicAdd",
    "shaderSharedFloat32Atomics",
    "shaderSharedFloat32AtomicAdd",
    "shaderSharedFloat64Atomics",
    "shaderSharedFloat64AtomicAdd",
    "shaderImageFloat32Atomics",
    "shaderImageFloat32AtomicAdd",
    "sparseImageFloat32Atomics",
    "sparseImageFloat32AtomicAdd",
    // VkPhysicalDeviceHostQueryResetFeatures
    "hostQueryReset",
    // VkPhysicalDeviceIndexTypeUint8Features
    "indexTypeUint8",
    // VkPhysicalDeviceExtendedDynamicStateFeaturesEXT
    "extendedDynamicState",
    // VkPhysicalDevicePipelineExecutablePropertiesFeaturesKHR
    "pipelineExecutableInfo",
    // VkPhysicalDeviceHostImageCopyFeatures
    "hostImageCopy",
    // VkPhysicalDeviceMapMemoryPlacedFeaturesEXT
    "memoryMapPlaced",
    "memoryMapRangePlaced",
    "memoryUnmapReserve",
    // VkPhysicalDeviceShaderAtomicFloat2FeaturesEXT
    "shaderBufferFloat16Atomics",
    "shaderBufferFloat16AtomicAdd",
    "shaderBufferFloat16AtomicMinMax",
    "shaderBufferFloat32AtomicMinMax",
    "shaderBufferFloat64AtomicMinMax",
    "shaderSharedFloat16Atomics",
    "shaderSharedFloat16AtomicAdd",
    "shaderSharedFloat16AtomicMinMax",
    "shaderSharedFloat32AtomicMinMax",
    "shaderSharedFloat64AtomicMinMax",
    "shaderImageFloat32AtomicMinMax",
    "sparseImageFloat32AtomicMinMax",
    // VkPhysicalDeviceSwapchainMaintenance1FeaturesKHR
    "swapchainMaintenance1",
    // VkPhysicalDeviceShaderDemoteToHelperInvocationFeatures
    "shaderDemoteToHelperInvocation",
    // VkPhysicalDeviceDeviceGeneratedCommandsFeaturesNV
    "deviceGeneratedCommands",
    // VkPhysicalDeviceInheritedViewportScissorFeaturesNV
    "inheritedViewportScissor2D",
    // VkPhysicalDeviceShaderIntegerDotProductFeatures
    "shaderIntegerDotProduct",
    // VkPhysicalDeviceTexelBufferAlignmentFeaturesEXT
    "texelBufferAlignment",
    // VkPhysicalDeviceDepthBiasControlFeaturesEXT
    "depthBiasControl",
    "leastRepresentableValueForceUnormRepresentation",
    "floatRepresentation",
    "depthBiasExact",
    // VkPhysicalDeviceDeviceMemoryReportFeaturesEXT
    "deviceMemoryReport",
    // VkPhysicalDeviceRobustness2FeaturesKHR
    "robustBufferAccess2",
    "robustImageAccess2",
    "nullDescriptor",
    // VkPhysicalDeviceCustomBorderColorFeaturesEXT
    "customBorderColors",
    "customBorderColorWithoutFormat",
    // VkPhysicalDeviceTextureCompressionASTC3DFeaturesEXT
    "textureCompressionASTC_3D",
    // VkPhysicalDevicePresentBarrierFeaturesNV
    "presentBarrier",
    // VkPhysicalDevicePresentIdFeaturesKHR
    "presentId",
    // VkPhysicalDevicePrivateDataFeatures
    "privateData",
    // VkPhysicalDevicePipelineCreationCacheControlFeatures
    "pipelineCreationCacheControl",
    // VkPhysicalDeviceVulkanSC10Features
    "shaderAtomicInstructions",
    // VkPhysicalDeviceDiagnosticsConfigFeaturesNV
    "diagnosticsConfig",
    // VkPhysicalDeviceQueuePerfHintFeaturesQCOM
    "queuePerfHint",
    // VkPhysicalDeviceImageProcessing3FeaturesQCOM
    "imageGatherLinear",
    "imageGatherExtendedModes",
    "blockMatchExtendedClampToEdge",
    // VkPhysicalDeviceShaderMultipleWaitQueuesFeaturesQCOM
    "shaderMultipleWaitQueues",
    // VkPhysicalDeviceShaderSplitBarrierFeaturesEXT
    "shaderSplitBarrier",
    // VkPhysicalDeviceCudaKernelLaunchFeaturesNV
    "cudaKernelLaunchFeatures",
    // VkPhysicalDeviceTileShadingFeaturesQCOM
    "tileShading",
    "tileShadingFragmentStage",
    "tileShadingColorAttachments",
    "tileShadingDepthAttachments",
    "tileShadingStencilAttachments",
    "tileShadingInputAttachments",
    "tileShadingSampledAttachments",
    "tileShadingPerTileDraw",
    "tileShadingPerTileDispatch",
    "tileShadingDispatchTile",
    "tileShadingApron",
    "tileShadingAnisotropicApron",
    "tileShadingAtomicOps",
    "tileShadingImageProcessing",
    // VkPhysicalDeviceSynchronization2Features
    "synchronization2",
    // VkPhysicalDeviceDescriptorBufferFeaturesEXT
    "descriptorBuffer",
    "descriptorBufferCaptureReplay",
    "descriptorBufferImageLayoutIgnored",
    "descriptorBufferPushDescriptors",
    // VkPhysicalDeviceDeviceAddressCommandsFeaturesKHR
    "deviceAddressCommands",
    // VkPhysicalDeviceGraphicsPipelineLibraryFeaturesEXT
    "graphicsPipelineLibrary",
    // VkPhysicalDeviceShaderEarlyAndLateFragmentTestsFeaturesAMD
    "shaderEarlyAndLateFragmentTests",
    // VkPhysicalDeviceShaderSubgroupUniformControlFlowFeaturesKHR
    "shaderSubgroupUniformControlFlow",
    // VkPhysicalDeviceZeroInitializeWorkgroupMemoryFeatures
    "shaderZeroInitializeWorkgroupMemory",
    // VkPhysicalDeviceFragmentShadingRateEnumsFeaturesNV
    "fragmentShadingRateEnums",
    "supersampleFragmentShadingRates",
    "noInvocationFragmentShadingRates",
    // VkPhysicalDeviceRayTracingMotionBlurFeaturesNV
    "rayTracingMotionBlur",
    "rayTracingMotionBlurPipelineTraceRaysIndirect",
    // VkPhysicalDeviceMeshShaderFeaturesEXT
    "taskShader",
    "meshShader",
    "multiviewMeshShader",
    "primitiveFragmentShadingRateMeshShader",
    "meshShaderQueries",
    // VkPhysicalDeviceYcbcr2Plane444FormatsFeaturesEXT
    "ycbcr2plane444Formats",
    // VkPhysicalDeviceFragmentDensityMap2FeaturesEXT
    "fragmentDensityMapDeferred",
    // VkPhysicalDeviceImageRobustnessFeatures
    "robustImageAccess",
    // VkPhysicalDeviceWorkgroupMemoryExplicitLayoutFeaturesKHR
    "workgroupMemoryExplicitLayout",
    "workgroupMemoryExplicitLayoutScalarBlockLayout",
    "workgroupMemoryExplicitLayout8BitAccess",
    "workgroupMemoryExplicitLayout16BitAccess",
    // VkPhysicalDeviceImageCompressionControlFeaturesEXT
    "imageCompressionControl",
    // VkPhysicalDeviceAttachmentFeedbackLoopLayoutFeaturesEXT
    "attachmentFeedbackLoopLayout",
    // VkPhysicalDevice4444FormatsFeaturesEXT
    "formatA4R4G4B4",
    "formatA4B4G4R4",
    // VkPhysicalDeviceFaultFeaturesEXT
    "deviceFault",
    "deviceFaultVendorBinary",
    // VkPhysicalDeviceRasterizationOrderAttachmentAccessFeaturesEXT
    "rasterizationOrderColorAttachmentAccess",
    "rasterizationOrderDepthAttachmentAccess",
    "rasterizationOrderStencilAttachmentAccess",
    // VkPhysicalDeviceRGBA10X6FormatsFeaturesEXT
    "formatRgba10x6WithoutYCbCrSampler",
    // VkPhysicalDeviceRayTracingPipelineFeaturesKHR
    "rayTracingPipeline",
    "rayTracingPipelineShaderGroupHandleCaptureReplay",
    "rayTracingPipelineShaderGroupHandleCaptureReplayMixed",
    "rayTracingPipelineTraceRaysIndirect",
    "rayTraversalPrimitiveCulling",
    // VkPhysicalDeviceRayQueryFeaturesKHR
    "rayQuery",
    // VkPhysicalDeviceMutableDescriptorTypeFeaturesEXT
    "mutableDescriptorType",
    // VkPhysicalDeviceVertexInputDynamicStateFeaturesEXT
    "vertexInputDynamicState",
    // VkPhysicalDeviceAddressBindingReportFeaturesEXT
    "reportAddressBinding",
    // VkPhysicalDeviceDepthClipControlFeaturesEXT
    "depthClipControl",
    // VkPhysicalDevicePrimitiveTopologyListRestartFeaturesEXT
    "primitiveTopologyListRestart",
    "primitiveTopologyPatchListRestart",
    // VkPhysicalDevicePresentModeFifoLatestReadyFeaturesKHR
    "presentModeFifoLatestReady",
    // VkPhysicalDeviceSubpassShadingFeaturesHUAWEI
    "subpassShading",
    // VkPhysicalDeviceInvocationMaskFeaturesHUAWEI
    "invocationMask",
    // VkPhysicalDeviceExternalMemoryRDMAFeaturesNV
    "externalMemoryRDMA",
    // VkPhysicalDevicePipelinePropertiesFeaturesEXT
    "pipelinePropertiesIdentifier",
    // VkPhysicalDeviceExternalSciSyncFeaturesNV
    "sciSyncFence",
    "sciSyncSemaphore",
    "sciSyncImport",
    "sciSyncExport",
    // VkPhysicalDeviceExternalMemorySciBufFeaturesNV
    "sciBufImport",
    "sciBufExport",
    // VkPhysicalDeviceFrameBoundaryFeaturesEXT
    "frameBoundary",
    // VkPhysicalDeviceMultisampledRenderToSingleSampledFeaturesEXT
    "multisampledRenderToSingleSampled",
    // VkPhysicalDeviceExtendedDynamicState2FeaturesEXT
    "extendedDynamicState2",
    "extendedDynamicState2LogicOp",
    "extendedDynamicState2PatchControlPoints",
    // VkPhysicalDeviceColorWriteEnableFeaturesEXT
    "colorWriteEnable",
    // VkPhysicalDevicePrimitivesGeneratedQueryFeaturesEXT
    "primitivesGeneratedQuery",
    "primitivesGeneratedQueryWithRasterizerDiscard",
    "primitivesGeneratedQueryWithNonZeroStreams",
    // VkPhysicalDeviceRayTracingMaintenance1FeaturesKHR
    "rayTracingMaintenance1",
    "rayTracingPipelineTraceRaysIndirect2",
    // VkPhysicalDeviceShaderUntypedPointersFeaturesKHR
    "shaderUntypedPointers",
    // VkPhysicalDeviceGlobalPriorityQueryFeatures
    "globalPriorityQuery",
    // VkPhysicalDeviceVideoEncodeRgbConversionFeaturesVALVE
    "videoEncodeRgbConversion",
    // VkPhysicalDeviceImageViewMinLodFeaturesEXT
    "minLod",
    // VkPhysicalDeviceMultiDrawFeaturesEXT
    "multiDraw",
    // VkPhysicalDeviceImage2DViewOf3DFeaturesEXT
    "image2DViewOf3D",
    "sampler2DViewOf3D",
    // VkPhysicalDeviceShaderTileImageFeaturesEXT
    "shaderTileImageColorReadAccess",
    "shaderTileImageDepthReadAccess",
    "shaderTileImageStencilReadAccess",
    // VkPhysicalDeviceOpacityMicromapFeaturesEXT
    "micromap",
    "micromapCaptureReplay",
    "micromapHostCommands",
    // VkPhysicalDeviceDisplacementMicromapFeaturesNV
    "displacementMicromap",
    // VkPhysicalDeviceClusterCullingShaderFeaturesHUAWEI
    "clustercullingShader",
    "multiviewClusterCullingShader",
    // VkPhysicalDeviceBorderColorSwizzleFeaturesEXT
    "borderColorSwizzle",
    "borderColorSwizzleFromImage",
    // VkPhysicalDevicePageableDeviceLocalMemoryFeaturesEXT
    "pageableDeviceLocalMemory",
    // VkPhysicalDeviceMaintenance4Features
    "maintenance4",
    // VkPhysicalDeviceShaderSubgroupRotateFeatures
    "shaderSubgroupRotate",
    "shaderSubgroupRotateClustered",
    // VkPhysicalDeviceSchedulingControlsFeaturesARM
    "schedulingControls",
    // VkPhysicalDeviceImageSlicedViewOf3DFeaturesEXT
    "imageSlicedViewOf3D",
    // VkPhysicalDeviceDescriptorSetHostMappingFeaturesVALVE
    "descriptorSetHostMapping",
    // VkPhysicalDeviceDepthClampZeroOneFeaturesKHR
    "depthClampZeroOne",
    // VkPhysicalDeviceNonSeamlessCubeMapFeaturesEXT
    "nonSeamlessCubeMap",
    // VkPhysicalDeviceRenderPassStripedFeaturesARM
    "renderPassStriped",
    // VkPhysicalDeviceFragmentDensityMapOffsetFeaturesEXT
    "fragmentDensityMapOffset",
    // VkPhysicalDeviceCopyMemoryIndirectFeaturesNV
    "indirectCopy",
    // VkPhysicalDeviceMemoryDecompressionFeaturesEXT
    "memoryDecompression",
    // VkPhysicalDeviceDeviceGeneratedCommandsComputeFeaturesNV
    "deviceGeneratedCompute",
    "deviceGeneratedComputePipelines",
    "deviceGeneratedComputeCaptureReplay",
    // VkPhysicalDeviceRayTracingLinearSweptSpheresFeaturesNV
    "spheres",
    "linearSweptSpheres",
    // VkPhysicalDeviceLinearColorAttachmentFeaturesNV
    "linearColorAttachment",
    // VkPhysicalDeviceShaderMaximalReconvergenceFeaturesKHR
    "shaderMaximalReconvergence",
    // VkPhysicalDeviceImageCompressionControlSwapchainFeaturesEXT
    "imageCompressionControlSwapchain",
    // VkPhysicalDeviceImageProcessingFeaturesQCOM
    "textureSampleWeighted",
    "textureBoxFilter",
    "textureBlockMatch",
    // VkPhysicalDeviceNestedCommandBufferFeaturesEXT
    "nestedCommandBuffer",
    "nestedCommandBufferRendering",
    "nestedCommandBufferSimultaneousUse",
    // VkPhysicalDeviceExtendedDynamicState3FeaturesEXT
    "extendedDynamicState3TessellationDomainOrigin",
    "extendedDynamicState3DepthClampEnable",
    "extendedDynamicState3PolygonMode",
    "extendedDynamicState3RasterizationSamples",
    "extendedDynamicState3SampleMask",
    "extendedDynamicState3AlphaToCoverageEnable",
    "extendedDynamicState3AlphaToOneEnable",
    "extendedDynamicState3LogicOpEnable",
    "extendedDynamicState3ColorBlendEnable",
    "extendedDynamicState3ColorBlendEquation",
    "extendedDynamicState3ColorWriteMask",
    "extendedDynamicState3RasterizationStream",
    "extendedDynamicState3ConservativeRasterizationMode",
    "extendedDynamicState3ExtraPrimitiveOverestimationSize",
    "extendedDynamicState3DepthClipEnable",
    "extendedDynamicState3SampleLocationsEnable",
    "extendedDynamicState3ColorBlendAdvanced",
    "extendedDynamicState3ProvokingVertexMode",
    "extendedDynamicState3LineRasterizationMode",
    "extendedDynamicState3LineStippleEnable",
    "extendedDynamicState3DepthClipNegativeOneToOne",
    "extendedDynamicState3ViewportWScalingEnable",
    "extendedDynamicState3ViewportSwizzle",
    "extendedDynamicState3CoverageToColorEnable",
    "extendedDynamicState3CoverageToColorLocation",
    "extendedDynamicState3CoverageModulationMode",
    "extendedDynamicState3CoverageModulationTableEnable",
    "extendedDynamicState3CoverageModulationTable",
    "extendedDynamicState3CoverageReductionMode",
    "extendedDynamicState3RepresentativeFragmentTestEnable",
    "extendedDynamicState3ShadingRateImageEnable",
    // VkPhysicalDeviceSubpassMergeFeedbackFeaturesEXT
    "subpassMergeFeedback",
    // VkPhysicalDeviceTensorFeaturesARM
    "tensorNonPacked",
    "shaderTensorAccess",
    "shaderStorageTensorArrayDynamicIndexing",
    "shaderStorageTensorArrayNonUniformIndexing",
    "descriptorBindingStorageTensorUpdateAfterBind",
    "tensors",
    // VkPhysicalDeviceDescriptorBufferTensorFeaturesARM
    "descriptorBufferTensorDescriptors",
    // VkPhysicalDeviceShaderModuleIdentifierFeaturesEXT
    "shaderModuleIdentifier",
    // VkPhysicalDeviceOpticalFlowFeaturesNV
    "opticalFlow",
    // VkPhysicalDeviceLegacyDitheringFeaturesEXT
    "legacyDithering",
    // VkPhysicalDevicePipelineProtectedAccessFeatures
    "pipelineProtectedAccess",
    // VkPhysicalDeviceExternalFormatResolveFeaturesANDROID
    "externalFormatResolve",
    // VkPhysicalDeviceMaintenance5Features
    "maintenance5",
    // VkPhysicalDeviceAntiLagFeaturesAMD
    "antiLag",
    // VkPhysicalDeviceDenseGeometryFormatFeaturesAMDX
    "denseGeometryFormat",
    // VkPhysicalDevicePresentId2FeaturesKHR
    "presentId2",
    // VkPhysicalDevicePresentWait2FeaturesKHR
    "presentWait2",
    // VkPhysicalDeviceRayTracingPositionFetchFeaturesKHR
    "rayTracingPositionFetch",
    // VkPhysicalDeviceShaderObjectFeaturesEXT
    "shaderObject",
    // VkPhysicalDevicePipelineBinaryFeaturesKHR
    "pipelineBinaries",
    // VkPhysicalDeviceTilePropertiesFeaturesQCOM
    "tileProperties",
    // VkPhysicalDeviceAmigoProfilingFeaturesSEC
    "amigoProfiling",
    // VkPhysicalDeviceMultiviewPerViewViewportsFeaturesQCOM
    "multiviewPerViewViewports",
    // VkPhysicalDeviceExternalSciSync2FeaturesNV
    "sciSyncFence",
    "sciSyncSemaphore2",
    "sciSyncImport",
    "sciSyncExport",
    // VkPhysicalDeviceRayTracingInvocationReorderFeaturesNV
    "rayTracingInvocationReorder",
    // VkPhysicalDeviceCooperativeVectorFeaturesNV
    "cooperativeVector",
    "cooperativeVectorTraining",
    // VkPhysicalDeviceExtendedSparseAddressSpaceFeaturesNV
    "extendedSparseAddressSpace",
    // VkPhysicalDeviceLegacyVertexAttributesFeaturesEXT
    "legacyVertexAttributes",
    // VkPhysicalDeviceShaderCoreBuiltinsFeaturesARM
    "shaderCoreBuiltins",
    // VkPhysicalDevicePipelineLibraryGroupHandlesFeaturesEXT
    "pipelineLibraryGroupHandles",
    // VkPhysicalDeviceDynamicRenderingUnusedAttachmentsFeaturesEXT
    "dynamicRenderingUnusedAttachments",
    // VkPhysicalDeviceInternallySynchronizedQueuesFeaturesKHR
    "internallySynchronizedQueues",
    // VkPhysicalDeviceCooperativeMatrixFeaturesKHR
    "cooperativeMatrix",
    "cooperativeMatrixRobustBufferAccess",
    // VkPhysicalDeviceDataGraphFeaturesARM
    "dataGraph",
    "dataGraphUpdateAfterBind",
    "dataGraphSpecializationConstants",
    "dataGraphDescriptorBuffer",
    "dataGraphShaderModule",
    // VkPhysicalDeviceMultiviewPerViewRenderAreasFeaturesQCOM
    "multiviewPerViewRenderAreas",
    // VkPhysicalDeviceVideoEncodeAV1FeaturesKHR
    "videoEncodeAV1",
    // VkPhysicalDeviceVideoDecodeVP9FeaturesKHR
    "videoDecodeVP9",
    // VkPhysicalDeviceVideoMaintenance1FeaturesKHR
    "videoMaintenance1",
    // VkPhysicalDevicePerStageDescriptorSetFeaturesNV
    "perStageDescriptorSet",
    "dynamicPipelineLayout",
    // VkPhysicalDeviceImageProcessing2FeaturesQCOM
    "textureBlockMatch2",
    // VkPhysicalDeviceCubicWeightsFeaturesQCOM
    "selectableCubicWeights",
    // VkPhysicalDeviceYcbcrDegammaFeaturesQCOM
    "ycbcrDegamma",
    // VkPhysicalDeviceCubicClampFeaturesQCOM
    "cubicRangeClamp",
    // VkPhysicalDeviceAttachmentFeedbackLoopDynamicStateFeaturesEXT
    "attachmentFeedbackLoopDynamicState",
    // VkPhysicalDeviceUnifiedImageLayoutsFeaturesKHR
    "unifiedImageLayouts",
    "unifiedImageLayoutsVideo",
    // VkPhysicalDeviceShaderFloatControls2Features
    "shaderFloatControls2",
    // VkPhysicalDeviceExternalMemoryScreenBufferFeaturesQNX
    "screenBufferImport",
    // VkPhysicalDeviceShaderExpectAssumeFeatures
    "shaderExpectAssume",
    // VkPhysicalDeviceMaintenance6Features
    "maintenance6",
    // VkPhysicalDeviceDescriptorPoolOverallocationFeaturesNV
    "descriptorPoolOverallocation",
    // VkPhysicalDeviceTileMemoryHeapFeaturesQCOM
    "tileMemoryHeap",
    // VkPhysicalDeviceCopyMemoryIndirectFeaturesKHR
    "indirectMemoryCopy",
    "indirectMemoryToImageCopy",
    // VkPhysicalDeviceVideoEncodeIntraRefreshFeaturesKHR
    "videoEncodeIntraRefresh",
    // VkPhysicalDeviceVideoEncodeQuantizationMapFeaturesKHR
    "videoEncodeQuantizationMap",
    // VkPhysicalDeviceRawAccessChainsFeaturesNV
    "shaderRawAccessChains",
    // VkPhysicalDeviceShaderRelaxedExtendedInstructionFeaturesKHR
    "shaderRelaxedExtendedInstruction",
    // VkPhysicalDeviceCommandBufferInheritanceFeaturesNV
    "commandBufferInheritance",
    // VkPhysicalDeviceMaintenance7FeaturesKHR
    "maintenance7",
    // VkPhysicalDeviceShaderAtomicFloat16VectorFeaturesNV
    "shaderFloat16VectorAtomics",
    // VkPhysicalDeviceShaderReplicatedCompositesFeaturesEXT
    "shaderReplicatedComposites",
    // VkPhysicalDeviceShaderFloat8FeaturesEXT
    "shaderFloat8",
    "shaderFloat8CooperativeMatrix",
    // VkPhysicalDeviceRayTracingValidationFeaturesNV
    "rayTracingValidation",
    // VkPhysicalDeviceClusterAccelerationStructureFeaturesNV
    "clusterAccelerationStructure",
    // VkPhysicalDevicePartitionedAccelerationStructureFeaturesNV
    "partitionedAccelerationStructure",
    // VkPhysicalDeviceDeviceGeneratedCommandsFeaturesEXT
    "deviceGeneratedCommands",
    "dynamicGeneratedPipelineLayout",
    // VkPhysicalDeviceFaultFeaturesKHR
    "deviceFault",
    "deviceFaultVendorBinary",
    "deviceFaultReportMasked",
    "deviceFaultDeviceLostOnMasked",
    // VkPhysicalDeviceMaintenance8FeaturesKHR
    "maintenance8",
    // VkPhysicalDeviceImageAlignmentControlFeaturesMESA
    "imageAlignmentControl",
    // VkPhysicalDeviceShaderFmaFeaturesKHR
    "shaderFmaFloat16",
    "shaderFmaFloat32",
    "shaderFmaFloat64",
    // VkPhysicalDevicePushConstantBankFeaturesNV
    "pushConstantBank",
    // VkPhysicalDeviceRayTracingInvocationReorderFeaturesEXT
    "rayTracingInvocationReorder",
    // VkPhysicalDeviceDepthClampControlFeaturesEXT
    "depthClampControl",
    // VkPhysicalDeviceMaintenance9FeaturesKHR
    "maintenance9",
    // VkPhysicalDeviceVideoMaintenance2FeaturesKHR
    "videoMaintenance2",
    // VkPhysicalDeviceHdrVividFeaturesHUAWEI
    "hdrVivid",
    // VkPhysicalDeviceCooperativeMatrix2FeaturesNV
    "cooperativeMatrixWorkgroupScope",
    "cooperativeMatrixFlexibleDimensions",
    "cooperativeMatrixReductions",
    "cooperativeMatrixConversions",
    "cooperativeMatrixPerElementOperations",
    "cooperativeMatrixTensorAddressing",
    "cooperativeMatrixBlockLoads",
    // VkPhysicalDevicePipelineOpacityMicromapFeaturesARM
    "pipelineOpacityMicromap",
    // VkPhysicalDeviceVideoEncodeFeedback2FeaturesKHR
    "videoEncodeFeedback2",
    // VkPhysicalDevicePerformanceCountersByRegionFeaturesARM
    "performanceCountersByRegion",
    // VkPhysicalDeviceShaderInstrumentationFeaturesARM
    "shaderInstrumentation",
    // VkPhysicalDeviceVertexAttributeRobustnessFeaturesEXT
    "vertexAttributeRobustness",
    // VkPhysicalDeviceFormatPackFeaturesARM
    "formatPack",
    // VkPhysicalDeviceFragmentDensityMapLayeredFeaturesVALVE
    "fragmentDensityMapLayered",
    // VkPhysicalDevicePresentMeteringFeaturesNV
    "presentMetering",
    // VkPhysicalDeviceMultisampledRenderToSwapchainFeaturesEXT
    "multisampledRenderToSwapchain",
    // VkPhysicalDeviceZeroInitializeDeviceMemoryFeaturesEXT
    "zeroInitializeDeviceMemory",
    // VkPhysicalDeviceOpacityMicromapFeaturesKHR
    "micromap",
    // VkPhysicalDeviceShader64BitIndexingFeaturesEXT
    "shader64BitIndexing",
    // VkPhysicalDeviceCustomResolveFeaturesEXT
    "customResolve",
    // VkPhysicalDeviceDataGraphModelFeaturesQCOM
    "dataGraphModel",
    // VkPhysicalDeviceMaintenance10FeaturesKHR
    "maintenance10",
    // VkPhysicalDeviceDataGraphOpticalFlowFeaturesARM
    "dataGraphOpticalFlow",
    // VkPhysicalDeviceShaderLongVectorFeaturesEXT
    "longVector",
    // VkPhysicalDevicePipelineCacheIncrementalModeFeaturesSEC
    "pipelineCacheIncrementalMode",
    // VkPhysicalDeviceShaderUniformBufferUnsizedArrayFeaturesEXT
    "shaderUniformBufferUnsizedArray",
    // VkPhysicalDeviceComputeOccupancyPriorityFeaturesNV
    "computeOccupancyPriority",
    // VkPhysicalDeviceMaintenance11FeaturesKHR
    "maintenance11",
    // VkPhysicalDeviceShaderSubgroupPartitionedFeaturesEXT
    "shaderSubgroupPartitioned",
    // VkPhysicalDeviceExtendedFlagsFeaturesKHR
    "extendedFlags",
    // VkPhysicalDeviceShaderOCPMicroscalingTypesFeaturesEXT
    "shaderFloat4",
    "shaderFloat6",
    "shaderFloat8UnsignedE8M0",
    "shaderMXInt8",
    // VkPhysicalDeviceShaderMixedFloatDotProductFeaturesVALVE
    "shaderMixedFloatDotProductFloat16AccFloat32",
    "shaderMixedFloatDotProductFloat16AccFloat16",
    "shaderMixedFloatDotProductBFloat16Acc",
    "shaderMixedFloatDotProductFloat8AccFloat32",
    // VkPhysicalDeviceThrottleHintFeaturesSEC
    "throttleHint",
    // VkPhysicalDeviceDataGraphNeuralAcceleratorStatisticsFeaturesARM
    "dataGraphNeuralAcceleratorStatistics",
    // VkPhysicalDevicePrimitiveRestartIndexFeaturesEXT
    "primitiveRestartIndex",
    // VkPhysicalDeviceCooperativeMatrixDecodeVectorFeaturesNV
    "cooperativeMatrixDecodeVector",
};

// sType (0: VkPhysicalDeviceFeatures, inside VkPhysicalDeviceFeatures2), the core version that
// has it as tens (13 is 1.3; 0 when none does), its first member in MEMBERS and their count
static const MaskStruct STRUCTS[] = {
    { 0, 10, 0, 55 }, // VkPhysicalDeviceFeatures
    { 49, 12, 55, 12 }, // VkPhysicalDeviceVulkan11Features
    { 51, 12, 67, 47 }, // VkPhysicalDeviceVulkan12Features
    { 53, 13, 114, 15 }, // VkPhysicalDeviceVulkan13Features
    { 55, 14, 129, 21 }, // VkPhysicalDeviceVulkan14Features
    { 1000028000, 0, 150, 2 }, // VkPhysicalDeviceTransformFeedbackFeaturesEXT
    { 1000044003, 13, 152, 1 }, // VkPhysicalDeviceDynamicRenderingFeatures
    { 1000050000, 0, 153, 1 }, // VkPhysicalDeviceCornerSampledImageFeaturesNV
    { 1000053001, 11, 154, 3 }, // VkPhysicalDeviceMultiviewFeatures
    { 1000063000, 11, 157, 1 }, // VkPhysicalDeviceShaderDrawParametersFeatures
    { 1000066000, 13, 158, 1 }, // VkPhysicalDeviceTextureCompressionASTCHDRFeatures
    { 1000067001, 0, 159, 1 }, // VkPhysicalDeviceASTCDecodeFeaturesEXT
    { 1000068001, 14, 160, 1 }, // VkPhysicalDevicePipelineRobustnessFeatures
    { 1000081001, 0, 161, 2 }, // VkPhysicalDeviceConditionalRenderingFeaturesEXT
    { 1000082000, 12, 163, 2 }, // VkPhysicalDeviceShaderFloat16Int8Features
    { 1000083000, 11, 165, 4 }, // VkPhysicalDevice16BitStorageFeatures
    { 1000102000, 0, 169, 1 }, // VkPhysicalDeviceDepthClipEnableFeaturesEXT
    { 1000108000, 12, 170, 1 }, // VkPhysicalDeviceImagelessFramebufferFeatures
    { 1000110000, 0, 171, 1 }, // VkPhysicalDeviceRelaxedLineRasterizationFeaturesIMG
    { 1000116000, 0, 172, 2 }, // VkPhysicalDevicePerformanceQueryFeaturesKHR
    { 1000120000, 11, 174, 2 }, // VkPhysicalDeviceVariablePointersFeatures
    { 1000133000, 0, 176, 4 }, // VkPhysicalDeviceGpaFeaturesAMD
    { 1000134000, 0, 180, 2 }, // VkPhysicalDeviceShaderEnqueueFeaturesAMDX
    { 1000135009, 0, 182, 2 }, // VkPhysicalDeviceDescriptorHeapFeaturesEXT
    { 1000138000, 13, 184, 2 }, // VkPhysicalDeviceInlineUniformBlockFeatures
    { 1000141000, 0, 186, 3 }, // VkPhysicalDeviceShaderBfloat16FeaturesKHR
    { 1000145001, 11, 189, 1 }, // VkPhysicalDeviceProtectedMemoryFeatures
    { 1000148000, 0, 190, 1 }, // VkPhysicalDeviceBlendOperationAdvancedFeaturesEXT
    { 1000150013, 0, 191, 5 }, // VkPhysicalDeviceAccelerationStructureFeaturesKHR
    { 1000154000, 0, 196, 1 }, // VkPhysicalDeviceShaderSMBuiltinsFeaturesNV
    { 1000156004, 11, 197, 1 }, // VkPhysicalDeviceSamplerYcbcrConversionFeatures
    { 1000161001, 12, 198, 20 }, // VkPhysicalDeviceDescriptorIndexingFeatures
    { 1000163000, 0, 218, 15 }, // VkPhysicalDevicePortabilitySubsetFeaturesKHR
    { 1000164001, 0, 233, 2 }, // VkPhysicalDeviceShadingRateImageFeaturesNV
    { 1000166000, 0, 235, 1 }, // VkPhysicalDeviceRepresentativeFragmentTestFeaturesNV
    { 1000172000, 0, 236, 1 }, // VkPhysicalDeviceCooperativeMatrixConversionFeaturesQCOM
    { 1000173000, 0, 237, 1 }, // VkPhysicalDeviceElapsedTimerQueryFeaturesQCOM
    { 1000175000, 12, 238, 1 }, // VkPhysicalDeviceShaderSubgroupExtendedTypesFeatures
    { 1000177000, 12, 239, 3 }, // VkPhysicalDevice8BitStorageFeatures
    { 1000180000, 12, 242, 2 }, // VkPhysicalDeviceShaderAtomicInt64Features
    { 1000181000, 0, 244, 2 }, // VkPhysicalDeviceShaderClockFeaturesKHR
    { 1000190002, 14, 246, 2 }, // VkPhysicalDeviceVertexAttributeDivisorFeatures
    { 1000201000, 0, 248, 2 }, // VkPhysicalDeviceComputeShaderDerivativesFeaturesKHR
    { 1000202000, 0, 250, 2 }, // VkPhysicalDeviceMeshShaderFeaturesNV
    { 1000203000, 0, 252, 1 }, // VkPhysicalDeviceFragmentShaderBarycentricFeaturesKHR
    { 1000204000, 0, 253, 1 }, // VkPhysicalDeviceShaderImageFootprintFeaturesNV
    { 1000205002, 0, 254, 1 }, // VkPhysicalDeviceExclusiveScissorFeaturesNV
    { 1000207000, 12, 255, 1 }, // VkPhysicalDeviceTimelineSemaphoreFeatures
    { 1000208000, 0, 256, 3 }, // VkPhysicalDevicePresentTimingFeaturesEXT
    { 1000209000, 0, 259, 1 }, // VkPhysicalDeviceShaderIntegerFunctions2FeaturesINTEL
    { 1000211000, 12, 260, 3 }, // VkPhysicalDeviceVulkanMemoryModelFeatures
    { 1000215000, 13, 263, 1 }, // VkPhysicalDeviceShaderTerminateInvocationFeatures
    { 1000218000, 0, 264, 3 }, // VkPhysicalDeviceFragmentDensityMapFeaturesEXT
    { 1000221000, 12, 267, 1 }, // VkPhysicalDeviceScalarBlockLayoutFeatures
    { 1000225002, 13, 268, 2 }, // VkPhysicalDeviceSubgroupSizeControlFeatures
    { 1000226003, 0, 270, 3 }, // VkPhysicalDeviceFragmentShadingRateFeaturesKHR
    { 1000229000, 0, 273, 1 }, // VkPhysicalDeviceCoherentMemoryFeaturesAMD
    { 1000231000, 0, 274, 1 }, // VkPhysicalDeviceShaderConstantDataFeaturesKHR
    { 1000232000, 14, 275, 1 }, // VkPhysicalDeviceDynamicRenderingLocalReadFeatures
    { 1000233000, 0, 276, 1 }, // VkPhysicalDeviceShaderAbortFeaturesKHR
    { 1000234000, 0, 277, 2 }, // VkPhysicalDeviceShaderImageAtomicInt64FeaturesEXT
    { 1000235000, 0, 279, 1 }, // VkPhysicalDeviceShaderQuadControlFeaturesKHR
    { 1000238000, 0, 280, 1 }, // VkPhysicalDeviceMemoryPriorityFeaturesEXT
    { 1000240000, 0, 281, 1 }, // VkPhysicalDeviceDedicatedAllocationImageAliasingFeaturesNV
    { 1000241000, 12, 282, 1 }, // VkPhysicalDeviceSeparateDepthStencilLayoutsFeatures
    { 1000244000, 0, 283, 3 }, // VkPhysicalDeviceBufferDeviceAddressFeaturesEXT
    { 1000248000, 0, 286, 1 }, // VkPhysicalDevicePresentWaitFeaturesKHR
    { 1000249000, 0, 287, 2 }, // VkPhysicalDeviceCooperativeMatrixFeaturesNV
    { 1000250000, 0, 289, 1 }, // VkPhysicalDeviceCoverageReductionModeFeaturesNV
    { 1000251000, 0, 290, 3 }, // VkPhysicalDeviceFragmentShaderInterlockFeaturesEXT
    { 1000252000, 0, 293, 1 }, // VkPhysicalDeviceYcbcrImageArraysFeaturesEXT
    { 1000253000, 12, 294, 1 }, // VkPhysicalDeviceUniformBufferStandardLayoutFeatures
    { 1000254000, 0, 295, 2 }, // VkPhysicalDeviceProvokingVertexFeaturesEXT
    { 1000257000, 12, 297, 3 }, // VkPhysicalDeviceBufferDeviceAddressFeatures
    { 1000259000, 14, 300, 6 }, // VkPhysicalDeviceLineRasterizationFeatures
    { 1000260000, 0, 306, 12 }, // VkPhysicalDeviceShaderAtomicFloatFeaturesEXT
    { 1000261000, 12, 318, 1 }, // VkPhysicalDeviceHostQueryResetFeatures
    { 1000265000, 14, 319, 1 }, // VkPhysicalDeviceIndexTypeUint8Features
    { 1000267000, 0, 320, 1 }, // VkPhysicalDeviceExtendedDynamicStateFeaturesEXT
    { 1000269000, 0, 321, 1 }, // VkPhysicalDevicePipelineExecutablePropertiesFeaturesKHR
    { 1000270000, 14, 322, 1 }, // VkPhysicalDeviceHostImageCopyFeatures
    { 1000272000, 0, 323, 3 }, // VkPhysicalDeviceMapMemoryPlacedFeaturesEXT
    { 1000273000, 0, 326, 12 }, // VkPhysicalDeviceShaderAtomicFloat2FeaturesEXT
    { 1000275000, 0, 338, 1 }, // VkPhysicalDeviceSwapchainMaintenance1FeaturesKHR
    { 1000276000, 13, 339, 1 }, // VkPhysicalDeviceShaderDemoteToHelperInvocationFeatures
    { 1000277007, 0, 340, 1 }, // VkPhysicalDeviceDeviceGeneratedCommandsFeaturesNV
    { 1000278000, 0, 341, 1 }, // VkPhysicalDeviceInheritedViewportScissorFeaturesNV
    { 1000280000, 13, 342, 1 }, // VkPhysicalDeviceShaderIntegerDotProductFeatures
    { 1000281000, 0, 343, 1 }, // VkPhysicalDeviceTexelBufferAlignmentFeaturesEXT
    { 1000283000, 0, 344, 4 }, // VkPhysicalDeviceDepthBiasControlFeaturesEXT
    { 1000284000, 0, 348, 1 }, // VkPhysicalDeviceDeviceMemoryReportFeaturesEXT
    { 1000286000, 0, 349, 3 }, // VkPhysicalDeviceRobustness2FeaturesKHR
    { 1000287002, 0, 352, 2 }, // VkPhysicalDeviceCustomBorderColorFeaturesEXT
    { 1000288000, 0, 354, 1 }, // VkPhysicalDeviceTextureCompressionASTC3DFeaturesEXT
    { 1000292000, 0, 355, 1 }, // VkPhysicalDevicePresentBarrierFeaturesNV
    { 1000294001, 0, 356, 1 }, // VkPhysicalDevicePresentIdFeaturesKHR
    { 1000295000, 13, 357, 1 }, // VkPhysicalDevicePrivateDataFeatures
    { 1000297000, 13, 358, 1 }, // VkPhysicalDevicePipelineCreationCacheControlFeatures
    { 1000298000, 0, 359, 1 }, // VkPhysicalDeviceVulkanSC10Features
    { 1000300000, 0, 360, 1 }, // VkPhysicalDeviceDiagnosticsConfigFeaturesNV
    { 1000302001, 0, 361, 1 }, // VkPhysicalDeviceQueuePerfHintFeaturesQCOM
    { 1000303000, 0, 362, 3 }, // VkPhysicalDeviceImageProcessing3FeaturesQCOM
    { 1000304000, 0, 365, 1 }, // VkPhysicalDeviceShaderMultipleWaitQueuesFeaturesQCOM
    { 1000305000, 0, 366, 1 }, // VkPhysicalDeviceShaderSplitBarrierFeaturesEXT
    { 1000307003, 0, 367, 1 }, // VkPhysicalDeviceCudaKernelLaunchFeaturesNV
    { 1000309000, 0, 368, 14 }, // VkPhysicalDeviceTileShadingFeaturesQCOM
    { 1000314007, 13, 382, 1 }, // VkPhysicalDeviceSynchronization2Features
    { 1000316002, 0, 383, 4 }, // VkPhysicalDeviceDescriptorBufferFeaturesEXT
    { 1000318006, 0, 387, 1 }, // VkPhysicalDeviceDeviceAddressCommandsFeaturesKHR
    { 1000320000, 0, 388, 1 }, // VkPhysicalDeviceGraphicsPipelineLibraryFeaturesEXT
    { 1000321000, 0, 389, 1 }, // VkPhysicalDeviceShaderEarlyAndLateFragmentTestsFeaturesAMD
    { 1000323000, 0, 390, 1 }, // VkPhysicalDeviceShaderSubgroupUniformControlFlowFeaturesKHR
    { 1000325000, 13, 391, 1 }, // VkPhysicalDeviceZeroInitializeWorkgroupMemoryFeatures
    { 1000326001, 0, 392, 3 }, // VkPhysicalDeviceFragmentShadingRateEnumsFeaturesNV
    { 1000327001, 0, 395, 2 }, // VkPhysicalDeviceRayTracingMotionBlurFeaturesNV
    { 1000328000, 0, 397, 5 }, // VkPhysicalDeviceMeshShaderFeaturesEXT
    { 1000330000, 0, 402, 1 }, // VkPhysicalDeviceYcbcr2Plane444FormatsFeaturesEXT
    { 1000332000, 0, 403, 1 }, // VkPhysicalDeviceFragmentDensityMap2FeaturesEXT
    { 1000335000, 13, 404, 1 }, // VkPhysicalDeviceImageRobustnessFeatures
    { 1000336000, 0, 405, 4 }, // VkPhysicalDeviceWorkgroupMemoryExplicitLayoutFeaturesKHR
    { 1000338000, 0, 409, 1 }, // VkPhysicalDeviceImageCompressionControlFeaturesEXT
    { 1000339000, 0, 410, 1 }, // VkPhysicalDeviceAttachmentFeedbackLoopLayoutFeaturesEXT
    { 1000340000, 0, 411, 2 }, // VkPhysicalDevice4444FormatsFeaturesEXT
    { 1000341000, 0, 413, 2 }, // VkPhysicalDeviceFaultFeaturesEXT
    { 1000342000, 0, 415, 3 }, // VkPhysicalDeviceRasterizationOrderAttachmentAccessFeaturesEXT
    { 1000344000, 0, 418, 1 }, // VkPhysicalDeviceRGBA10X6FormatsFeaturesEXT
    { 1000347000, 0, 419, 5 }, // VkPhysicalDeviceRayTracingPipelineFeaturesKHR
    { 1000348013, 0, 424, 1 }, // VkPhysicalDeviceRayQueryFeaturesKHR
    { 1000351000, 0, 425, 1 }, // VkPhysicalDeviceMutableDescriptorTypeFeaturesEXT
    { 1000352000, 0, 426, 1 }, // VkPhysicalDeviceVertexInputDynamicStateFeaturesEXT
    { 1000354000, 0, 427, 1 }, // VkPhysicalDeviceAddressBindingReportFeaturesEXT
    { 1000355000, 0, 428, 1 }, // VkPhysicalDeviceDepthClipControlFeaturesEXT
    { 1000356000, 0, 429, 2 }, // VkPhysicalDevicePrimitiveTopologyListRestartFeaturesEXT
    { 1000361000, 0, 431, 1 }, // VkPhysicalDevicePresentModeFifoLatestReadyFeaturesKHR
    { 1000369001, 0, 432, 1 }, // VkPhysicalDeviceSubpassShadingFeaturesHUAWEI
    { 1000370000, 0, 433, 1 }, // VkPhysicalDeviceInvocationMaskFeaturesHUAWEI
    { 1000371001, 0, 434, 1 }, // VkPhysicalDeviceExternalMemoryRDMAFeaturesNV
    { 1000372001, 0, 435, 1 }, // VkPhysicalDevicePipelinePropertiesFeaturesEXT
    { 1000373007, 0, 436, 4 }, // VkPhysicalDeviceExternalSciSyncFeaturesNV
    { 1000374004, 0, 440, 2 }, // VkPhysicalDeviceExternalMemorySciBufFeaturesNV
    { 1000375000, 0, 442, 1 }, // VkPhysicalDeviceFrameBoundaryFeaturesEXT
    { 1000376000, 0, 443, 1 }, // VkPhysicalDeviceMultisampledRenderToSingleSampledFeaturesEXT
    { 1000377000, 0, 444, 3 }, // VkPhysicalDeviceExtendedDynamicState2FeaturesEXT
    { 1000381000, 0, 447, 1 }, // VkPhysicalDeviceColorWriteEnableFeaturesEXT
    { 1000382000, 0, 448, 3 }, // VkPhysicalDevicePrimitivesGeneratedQueryFeaturesEXT
    { 1000386000, 0, 451, 2 }, // VkPhysicalDeviceRayTracingMaintenance1FeaturesKHR
    { 1000387000, 0, 453, 1 }, // VkPhysicalDeviceShaderUntypedPointersFeaturesKHR
    { 1000388000, 14, 454, 1 }, // VkPhysicalDeviceGlobalPriorityQueryFeatures
    { 1000390000, 0, 455, 1 }, // VkPhysicalDeviceVideoEncodeRgbConversionFeaturesVALVE
    { 1000391000, 0, 456, 1 }, // VkPhysicalDeviceImageViewMinLodFeaturesEXT
    { 1000392000, 0, 457, 1 }, // VkPhysicalDeviceMultiDrawFeaturesEXT
    { 1000393000, 0, 458, 2 }, // VkPhysicalDeviceImage2DViewOf3DFeaturesEXT
    { 1000395000, 0, 460, 3 }, // VkPhysicalDeviceShaderTileImageFeaturesEXT
    { 1000396005, 0, 463, 3 }, // VkPhysicalDeviceOpacityMicromapFeaturesEXT
    { 1000397000, 0, 466, 1 }, // VkPhysicalDeviceDisplacementMicromapFeaturesNV
    { 1000404000, 0, 467, 2 }, // VkPhysicalDeviceClusterCullingShaderFeaturesHUAWEI
    { 1000411000, 0, 469, 2 }, // VkPhysicalDeviceBorderColorSwizzleFeaturesEXT
    { 1000412000, 0, 471, 1 }, // VkPhysicalDevicePageableDeviceLocalMemoryFeaturesEXT
    { 1000413000, 13, 472, 1 }, // VkPhysicalDeviceMaintenance4Features
    { 1000416000, 14, 473, 2 }, // VkPhysicalDeviceShaderSubgroupRotateFeatures
    { 1000417001, 0, 475, 1 }, // VkPhysicalDeviceSchedulingControlsFeaturesARM
    { 1000418000, 0, 476, 1 }, // VkPhysicalDeviceImageSlicedViewOf3DFeaturesEXT
    { 1000420000, 0, 477, 1 }, // VkPhysicalDeviceDescriptorSetHostMappingFeaturesVALVE
    { 1000421000, 0, 478, 1 }, // VkPhysicalDeviceDepthClampZeroOneFeaturesKHR
    { 1000422000, 0, 479, 1 }, // VkPhysicalDeviceNonSeamlessCubeMapFeaturesEXT
    { 1000424000, 0, 480, 1 }, // VkPhysicalDeviceRenderPassStripedFeaturesARM
    { 1000425000, 0, 481, 1 }, // VkPhysicalDeviceFragmentDensityMapOffsetFeaturesEXT
    { 1000426000, 0, 482, 1 }, // VkPhysicalDeviceCopyMemoryIndirectFeaturesNV
    { 1000427000, 0, 483, 1 }, // VkPhysicalDeviceMemoryDecompressionFeaturesEXT
    { 1000428000, 0, 484, 3 }, // VkPhysicalDeviceDeviceGeneratedCommandsComputeFeaturesNV
    { 1000429008, 0, 487, 2 }, // VkPhysicalDeviceRayTracingLinearSweptSpheresFeaturesNV
    { 1000430000, 0, 489, 1 }, // VkPhysicalDeviceLinearColorAttachmentFeaturesNV
    { 1000434000, 0, 490, 1 }, // VkPhysicalDeviceShaderMaximalReconvergenceFeaturesKHR
    { 1000437000, 0, 491, 1 }, // VkPhysicalDeviceImageCompressionControlSwapchainFeaturesEXT
    { 1000440000, 0, 492, 3 }, // VkPhysicalDeviceImageProcessingFeaturesQCOM
    { 1000451000, 0, 495, 3 }, // VkPhysicalDeviceNestedCommandBufferFeaturesEXT
    { 1000455000, 0, 498, 31 }, // VkPhysicalDeviceExtendedDynamicState3FeaturesEXT
    { 1000458000, 0, 529, 1 }, // VkPhysicalDeviceSubpassMergeFeedbackFeaturesEXT
    { 1000460009, 0, 530, 6 }, // VkPhysicalDeviceTensorFeaturesARM
    { 1000460018, 0, 536, 1 }, // VkPhysicalDeviceDescriptorBufferTensorFeaturesARM
    { 1000462000, 0, 537, 1 }, // VkPhysicalDeviceShaderModuleIdentifierFeaturesEXT
    { 1000464000, 0, 538, 1 }, // VkPhysicalDeviceOpticalFlowFeaturesNV
    { 1000465000, 0, 539, 1 }, // VkPhysicalDeviceLegacyDitheringFeaturesEXT
    { 1000466000, 14, 540, 1 }, // VkPhysicalDevicePipelineProtectedAccessFeatures
    { 1000468000, 0, 541, 1 }, // VkPhysicalDeviceExternalFormatResolveFeaturesANDROID
    { 1000470000, 14, 542, 1 }, // VkPhysicalDeviceMaintenance5Features
    { 1000476000, 0, 543, 1 }, // VkPhysicalDeviceAntiLagFeaturesAMD
    { 1000478000, 0, 544, 1 }, // VkPhysicalDeviceDenseGeometryFormatFeaturesAMDX
    { 1000479002, 0, 545, 1 }, // VkPhysicalDevicePresentId2FeaturesKHR
    { 1000480001, 0, 546, 1 }, // VkPhysicalDevicePresentWait2FeaturesKHR
    { 1000481000, 0, 547, 1 }, // VkPhysicalDeviceRayTracingPositionFetchFeaturesKHR
    { 1000482000, 0, 548, 1 }, // VkPhysicalDeviceShaderObjectFeaturesEXT
    { 1000483000, 0, 549, 1 }, // VkPhysicalDevicePipelineBinaryFeaturesKHR
    { 1000484000, 0, 550, 1 }, // VkPhysicalDeviceTilePropertiesFeaturesQCOM
    { 1000485000, 0, 551, 1 }, // VkPhysicalDeviceAmigoProfilingFeaturesSEC
    { 1000488000, 0, 552, 1 }, // VkPhysicalDeviceMultiviewPerViewViewportsFeaturesQCOM
    { 1000489002, 0, 553, 4 }, // VkPhysicalDeviceExternalSciSync2FeaturesNV
    { 1000490000, 0, 557, 1 }, // VkPhysicalDeviceRayTracingInvocationReorderFeaturesNV
    { 1000491000, 0, 558, 2 }, // VkPhysicalDeviceCooperativeVectorFeaturesNV
    { 1000492000, 0, 560, 1 }, // VkPhysicalDeviceExtendedSparseAddressSpaceFeaturesNV
    { 1000495000, 0, 561, 1 }, // VkPhysicalDeviceLegacyVertexAttributesFeaturesEXT
    { 1000497000, 0, 562, 1 }, // VkPhysicalDeviceShaderCoreBuiltinsFeaturesARM
    { 1000498000, 0, 563, 1 }, // VkPhysicalDevicePipelineLibraryGroupHandlesFeaturesEXT
    { 1000499000, 0, 564, 1 }, // VkPhysicalDeviceDynamicRenderingUnusedAttachmentsFeaturesEXT
    { 1000504000, 0, 565, 1 }, // VkPhysicalDeviceInternallySynchronizedQueuesFeaturesKHR
    { 1000506000, 0, 566, 2 }, // VkPhysicalDeviceCooperativeMatrixFeaturesKHR
    { 1000507006, 0, 568, 5 }, // VkPhysicalDeviceDataGraphFeaturesARM
    { 1000510000, 0, 573, 1 }, // VkPhysicalDeviceMultiviewPerViewRenderAreasFeaturesQCOM
    { 1000513004, 0, 574, 1 }, // VkPhysicalDeviceVideoEncodeAV1FeaturesKHR
    { 1000514000, 0, 575, 1 }, // VkPhysicalDeviceVideoDecodeVP9FeaturesKHR
    { 1000515000, 0, 576, 1 }, // VkPhysicalDeviceVideoMaintenance1FeaturesKHR
    { 1000516000, 0, 577, 2 }, // VkPhysicalDevicePerStageDescriptorSetFeaturesNV
    { 1000518000, 0, 579, 1 }, // VkPhysicalDeviceImageProcessing2FeaturesQCOM
    { 1000519001, 0, 580, 1 }, // VkPhysicalDeviceCubicWeightsFeaturesQCOM
    { 1000520000, 0, 581, 1 }, // VkPhysicalDeviceYcbcrDegammaFeaturesQCOM
    { 1000521000, 0, 582, 1 }, // VkPhysicalDeviceCubicClampFeaturesQCOM
    { 1000524000, 0, 583, 1 }, // VkPhysicalDeviceAttachmentFeedbackLoopDynamicStateFeaturesEXT
    { 1000527000, 0, 584, 2 }, // VkPhysicalDeviceUnifiedImageLayoutsFeaturesKHR
    { 1000528000, 14, 586, 1 }, // VkPhysicalDeviceShaderFloatControls2Features
    { 1000529004, 0, 587, 1 }, // VkPhysicalDeviceExternalMemoryScreenBufferFeaturesQNX
    { 1000544000, 14, 588, 1 }, // VkPhysicalDeviceShaderExpectAssumeFeatures
    { 1000545000, 14, 589, 1 }, // VkPhysicalDeviceMaintenance6Features
    { 1000546000, 0, 590, 1 }, // VkPhysicalDeviceDescriptorPoolOverallocationFeaturesNV
    { 1000547000, 0, 591, 1 }, // VkPhysicalDeviceTileMemoryHeapFeaturesQCOM
    { 1000549000, 0, 592, 2 }, // VkPhysicalDeviceCopyMemoryIndirectFeaturesKHR
    { 1000552004, 0, 594, 1 }, // VkPhysicalDeviceVideoEncodeIntraRefreshFeaturesKHR
    { 1000553009, 0, 595, 1 }, // VkPhysicalDeviceVideoEncodeQuantizationMapFeaturesKHR
    { 1000555000, 0, 596, 1 }, // VkPhysicalDeviceRawAccessChainsFeaturesNV
    { 1000558000, 0, 597, 1 }, // VkPhysicalDeviceShaderRelaxedExtendedInstructionFeaturesKHR
    { 1000559000, 0, 598, 1 }, // VkPhysicalDeviceCommandBufferInheritanceFeaturesNV
    { 1000562000, 0, 599, 1 }, // VkPhysicalDeviceMaintenance7FeaturesKHR
    { 1000563000, 0, 600, 1 }, // VkPhysicalDeviceShaderAtomicFloat16VectorFeaturesNV
    { 1000564000, 0, 601, 1 }, // VkPhysicalDeviceShaderReplicatedCompositesFeaturesEXT
    { 1000567000, 0, 602, 2 }, // VkPhysicalDeviceShaderFloat8FeaturesEXT
    { 1000568000, 0, 604, 1 }, // VkPhysicalDeviceRayTracingValidationFeaturesNV
    { 1000569000, 0, 605, 1 }, // VkPhysicalDeviceClusterAccelerationStructureFeaturesNV
    { 1000570000, 0, 606, 1 }, // VkPhysicalDevicePartitionedAccelerationStructureFeaturesNV
    { 1000572000, 0, 607, 2 }, // VkPhysicalDeviceDeviceGeneratedCommandsFeaturesEXT
    { 1000573000, 0, 609, 4 }, // VkPhysicalDeviceFaultFeaturesKHR
    { 1000574000, 0, 613, 1 }, // VkPhysicalDeviceMaintenance8FeaturesKHR
    { 1000575000, 0, 614, 1 }, // VkPhysicalDeviceImageAlignmentControlFeaturesMESA
    { 1000579000, 0, 615, 3 }, // VkPhysicalDeviceShaderFmaFeaturesKHR
    { 1000580001, 0, 618, 1 }, // VkPhysicalDevicePushConstantBankFeaturesNV
    { 1000581000, 0, 619, 1 }, // VkPhysicalDeviceRayTracingInvocationReorderFeaturesEXT
    { 1000582000, 0, 620, 1 }, // VkPhysicalDeviceDepthClampControlFeaturesEXT
    { 1000584000, 0, 621, 1 }, // VkPhysicalDeviceMaintenance9FeaturesKHR
    { 1000586000, 0, 622, 1 }, // VkPhysicalDeviceVideoMaintenance2FeaturesKHR
    { 1000590000, 0, 623, 1 }, // VkPhysicalDeviceHdrVividFeaturesHUAWEI
    { 1000593000, 0, 624, 7 }, // VkPhysicalDeviceCooperativeMatrix2FeaturesNV
    { 1000596000, 0, 631, 1 }, // VkPhysicalDevicePipelineOpacityMicromapFeaturesARM
    { 1000598000, 0, 632, 1 }, // VkPhysicalDeviceVideoEncodeFeedback2FeaturesKHR
    { 1000605000, 0, 633, 1 }, // VkPhysicalDevicePerformanceCountersByRegionFeaturesARM
    { 1000607000, 0, 634, 1 }, // VkPhysicalDeviceShaderInstrumentationFeaturesARM
    { 1000608000, 0, 635, 1 }, // VkPhysicalDeviceVertexAttributeRobustnessFeaturesEXT
    { 1000609000, 0, 636, 1 }, // VkPhysicalDeviceFormatPackFeaturesARM
    { 1000611000, 0, 637, 1 }, // VkPhysicalDeviceFragmentDensityMapLayeredFeaturesVALVE
    { 1000613001, 0, 638, 1 }, // VkPhysicalDevicePresentMeteringFeaturesNV
    { 1000616000, 0, 639, 1 }, // VkPhysicalDeviceMultisampledRenderToSwapchainFeaturesEXT
    { 1000620000, 0, 640, 1 }, // VkPhysicalDeviceZeroInitializeDeviceMemoryFeaturesEXT
    { 1000623000, 0, 641, 1 }, // VkPhysicalDeviceOpacityMicromapFeaturesKHR
    { 1000627000, 0, 642, 1 }, // VkPhysicalDeviceShader64BitIndexingFeaturesEXT
    { 1000628000, 0, 643, 1 }, // VkPhysicalDeviceCustomResolveFeaturesEXT
    { 1000629000, 0, 644, 1 }, // VkPhysicalDeviceDataGraphModelFeaturesQCOM
    { 1000630000, 0, 645, 1 }, // VkPhysicalDeviceMaintenance10FeaturesKHR
    { 1000631000, 0, 646, 1 }, // VkPhysicalDeviceDataGraphOpticalFlowFeaturesARM
    { 1000635000, 0, 647, 1 }, // VkPhysicalDeviceShaderLongVectorFeaturesEXT
    { 1000637000, 0, 648, 1 }, // VkPhysicalDevicePipelineCacheIncrementalModeFeaturesSEC
    { 1000642000, 0, 649, 1 }, // VkPhysicalDeviceShaderUniformBufferUnsizedArrayFeaturesEXT
    { 1000645001, 0, 650, 1 }, // VkPhysicalDeviceComputeOccupancyPriorityFeaturesNV
    { 1000657000, 0, 651, 1 }, // VkPhysicalDeviceMaintenance11FeaturesKHR
    { 1000662000, 0, 652, 1 }, // VkPhysicalDeviceShaderSubgroupPartitionedFeaturesEXT
    { 1000668004, 0, 653, 1 }, // VkPhysicalDeviceExtendedFlagsFeaturesKHR
    { 1000672000, 0, 654, 4 }, // VkPhysicalDeviceShaderOCPMicroscalingTypesFeaturesEXT
    { 1000673000, 0, 658, 4 }, // VkPhysicalDeviceShaderMixedFloatDotProductFeaturesVALVE
    { 1000674000, 0, 662, 1 }, // VkPhysicalDeviceThrottleHintFeaturesSEC
    { 1000676002, 0, 663, 1 }, // VkPhysicalDeviceDataGraphNeuralAcceleratorStatisticsFeaturesARM
    { 1000678000, 0, 664, 1 }, // VkPhysicalDevicePrimitiveRestartIndexFeaturesEXT
    { 1000689000, 0, 665, 1 }, // VkPhysicalDeviceCooperativeMatrixDecodeVectorFeaturesNV
};

// each extension that adds a feature structure, with that structure's place in STRUCTS
static const MaskOwner OWNERS[] = {
    { "VK_EXT_transform_feedback", 5 },
    { "VK_KHR_dynamic_rendering", 6 },
    { "VK_NV_corner_sampled_image", 7 },
    { "VK_KHR_multiview", 8 },
    { "VK_EXT_texture_compression_astc_hdr", 10 },
    { "VK_EXT_astc_decode_mode", 11 },
    { "VK_EXT_pipeline_robustness", 12 },
    { "VK_EXT_conditional_rendering", 13 },
    { "VK_KHR_shader_float16_int8", 14 },
    { "VK_KHR_16bit_storage", 15 },
    { "VK_EXT_depth_clip_enable", 16 },
    { "VK_KHR_imageless_framebuffer", 17 },
    { "VK_IMG_relaxed_line_rasterization", 18 },
    { "VK_KHR_performance_query", 19 },
    { "VK_KHR_variable_pointers", 20 },
    { "VK_AMD_gpa_interface", 21 },
    { "VK_AMDX_shader_enqueue", 22 },
    { "VK_EXT_descriptor_heap", 23 },
    { "VK_EXT_inline_uniform_block", 24 },
    { "VK_KHR_shader_bfloat16", 25 },
    { "VK_EXT_blend_operation_advanced", 27 },
    { "VK_KHR_acceleration_structure", 28 },
    { "VK_NV_shader_sm_builtins", 29 },
    { "VK_KHR_sampler_ycbcr_conversion", 30 },
    { "VK_EXT_descriptor_indexing", 31 },
    { "VK_KHR_portability_subset", 32 },
    { "VK_NV_shading_rate_image", 33 },
    { "VK_NV_representative_fragment_test", 34 },
    { "VK_QCOM_cooperative_matrix_conversion", 35 },
    { "VK_QCOM_elapsed_timer_query", 36 },
    { "VK_KHR_shader_subgroup_extended_types", 37 },
    { "VK_KHR_8bit_storage", 38 },
    { "VK_KHR_shader_atomic_int64", 39 },
    { "VK_KHR_shader_clock", 40 },
    { "VK_EXT_vertex_attribute_divisor", 41 },
    { "VK_KHR_vertex_attribute_divisor", 41 },
    { "VK_KHR_compute_shader_derivatives", 42 },
    { "VK_NV_compute_shader_derivatives", 42 },
    { "VK_NV_mesh_shader", 43 },
    { "VK_KHR_fragment_shader_barycentric", 44 },
    { "VK_NV_fragment_shader_barycentric", 44 },
    { "VK_NV_shader_image_footprint", 45 },
    { "VK_NV_scissor_exclusive", 46 },
    { "VK_KHR_timeline_semaphore", 47 },
    { "VK_EXT_present_timing", 48 },
    { "VK_INTEL_shader_integer_functions2", 49 },
    { "VK_KHR_vulkan_memory_model", 50 },
    { "VK_KHR_shader_terminate_invocation", 51 },
    { "VK_EXT_fragment_density_map", 52 },
    { "VK_EXT_scalar_block_layout", 53 },
    { "VK_EXT_subgroup_size_control", 54 },
    { "VK_KHR_fragment_shading_rate", 55 },
    { "VK_AMD_device_coherent_memory", 56 },
    { "VK_KHR_shader_constant_data", 57 },
    { "VK_KHR_dynamic_rendering_local_read", 58 },
    { "VK_KHR_shader_abort", 59 },
    { "VK_EXT_shader_image_atomic_int64", 60 },
    { "VK_KHR_shader_quad_control", 61 },
    { "VK_EXT_memory_priority", 62 },
    { "VK_NV_dedicated_allocation_image_aliasing", 63 },
    { "VK_KHR_separate_depth_stencil_layouts", 64 },
    { "VK_EXT_buffer_device_address", 65 },
    { "VK_KHR_present_wait", 66 },
    { "VK_NV_cooperative_matrix", 67 },
    { "VK_NV_coverage_reduction_mode", 68 },
    { "VK_EXT_fragment_shader_interlock", 69 },
    { "VK_EXT_ycbcr_image_arrays", 70 },
    { "VK_KHR_uniform_buffer_standard_layout", 71 },
    { "VK_EXT_provoking_vertex", 72 },
    { "VK_KHR_buffer_device_address", 73 },
    { "VK_EXT_line_rasterization", 74 },
    { "VK_KHR_line_rasterization", 74 },
    { "VK_EXT_shader_atomic_float", 75 },
    { "VK_EXT_host_query_reset", 76 },
    { "VK_EXT_index_type_uint8", 77 },
    { "VK_KHR_index_type_uint8", 77 },
    { "VK_EXT_extended_dynamic_state", 78 },
    { "VK_KHR_pipeline_executable_properties", 79 },
    { "VK_EXT_host_image_copy", 80 },
    { "VK_EXT_map_memory_placed", 81 },
    { "VK_EXT_shader_atomic_float2", 82 },
    { "VK_EXT_swapchain_maintenance1", 83 },
    { "VK_KHR_swapchain_maintenance1", 83 },
    { "VK_EXT_shader_demote_to_helper_invocation", 84 },
    { "VK_NV_device_generated_commands", 85 },
    { "VK_NV_inherited_viewport_scissor", 86 },
    { "VK_KHR_shader_integer_dot_product", 87 },
    { "VK_EXT_texel_buffer_alignment", 88 },
    { "VK_EXT_depth_bias_control", 89 },
    { "VK_EXT_device_memory_report", 90 },
    { "VK_EXT_robustness2", 91 },
    { "VK_KHR_robustness2", 91 },
    { "VK_EXT_custom_border_color", 92 },
    { "VK_EXT_texture_compression_astc_3d", 93 },
    { "VK_NV_present_barrier", 94 },
    { "VK_KHR_present_id", 95 },
    { "VK_EXT_private_data", 96 },
    { "VK_EXT_pipeline_creation_cache_control", 97 },
    { "VK_NV_device_diagnostics_config", 99 },
    { "VK_QCOM_queue_perf_hint", 100 },
    { "VK_QCOM_image_processing3", 101 },
    { "VK_QCOM_shader_multiple_wait_queues", 102 },
    { "VK_EXT_shader_split_barrier", 103 },
    { "VK_NV_cuda_kernel_launch", 104 },
    { "VK_QCOM_tile_shading", 105 },
    { "VK_KHR_synchronization2", 106 },
    { "VK_EXT_descriptor_buffer", 107 },
    { "VK_KHR_device_address_commands", 108 },
    { "VK_EXT_graphics_pipeline_library", 109 },
    { "VK_AMD_shader_early_and_late_fragment_tests", 110 },
    { "VK_KHR_shader_subgroup_uniform_control_flow", 111 },
    { "VK_KHR_zero_initialize_workgroup_memory", 112 },
    { "VK_NV_fragment_shading_rate_enums", 113 },
    { "VK_NV_ray_tracing_motion_blur", 114 },
    { "VK_EXT_mesh_shader", 115 },
    { "VK_EXT_ycbcr_2plane_444_formats", 116 },
    { "VK_EXT_fragment_density_map2", 117 },
    { "VK_EXT_image_robustness", 118 },
    { "VK_KHR_workgroup_memory_explicit_layout", 119 },
    { "VK_EXT_image_compression_control", 120 },
    { "VK_EXT_attachment_feedback_loop_layout", 121 },
    { "VK_EXT_4444_formats", 122 },
    { "VK_EXT_device_fault", 123 },
    { "VK_ARM_rasterization_order_attachment_access", 124 },
    { "VK_EXT_rasterization_order_attachment_access", 124 },
    { "VK_EXT_rgba10x6_formats", 125 },
    { "VK_KHR_ray_tracing_pipeline", 126 },
    { "VK_KHR_ray_query", 127 },
    { "VK_EXT_mutable_descriptor_type", 128 },
    { "VK_VALVE_mutable_descriptor_type", 128 },
    { "VK_EXT_vertex_input_dynamic_state", 129 },
    { "VK_EXT_device_address_binding_report", 130 },
    { "VK_EXT_depth_clip_control", 131 },
    { "VK_EXT_primitive_topology_list_restart", 132 },
    { "VK_EXT_present_mode_fifo_latest_ready", 133 },
    { "VK_KHR_present_mode_fifo_latest_ready", 133 },
    { "VK_HUAWEI_subpass_shading", 134 },
    { "VK_HUAWEI_invocation_mask", 135 },
    { "VK_NV_external_memory_rdma", 136 },
    { "VK_EXT_pipeline_properties", 137 },
    { "VK_EXT_frame_boundary", 140 },
    { "VK_EXT_multisampled_render_to_single_sampled", 141 },
    { "VK_EXT_extended_dynamic_state2", 142 },
    { "VK_EXT_color_write_enable", 143 },
    { "VK_EXT_primitives_generated_query", 144 },
    { "VK_KHR_ray_tracing_maintenance1", 145 },
    { "VK_KHR_shader_untyped_pointers", 146 },
    { "VK_EXT_global_priority_query", 147 },
    { "VK_KHR_global_priority", 147 },
    { "VK_VALVE_video_encode_rgb_conversion", 148 },
    { "VK_EXT_image_view_min_lod", 149 },
    { "VK_EXT_multi_draw", 150 },
    { "VK_EXT_image_2d_view_of_3d", 151 },
    { "VK_EXT_shader_tile_image", 152 },
    { "VK_EXT_opacity_micromap", 153 },
    { "VK_NV_displacement_micromap", 154 },
    { "VK_HUAWEI_cluster_culling_shader", 155 },
    { "VK_EXT_border_color_swizzle", 156 },
    { "VK_EXT_pageable_device_local_memory", 157 },
    { "VK_KHR_maintenance4", 158 },
    { "VK_KHR_shader_subgroup_rotate", 159 },
    { "VK_ARM_scheduling_controls", 160 },
    { "VK_EXT_image_sliced_view_of_3d", 161 },
    { "VK_VALVE_descriptor_set_host_mapping", 162 },
    { "VK_EXT_depth_clamp_zero_one", 163 },
    { "VK_KHR_depth_clamp_zero_one", 163 },
    { "VK_EXT_non_seamless_cube_map", 164 },
    { "VK_ARM_render_pass_striped", 165 },
    { "VK_EXT_fragment_density_map_offset", 166 },
    { "VK_QCOM_fragment_density_map_offset", 166 },
    { "VK_NV_copy_memory_indirect", 167 },
    { "VK_EXT_memory_decompression", 168 },
    { "VK_NV_memory_decompression", 168 },
    { "VK_NV_device_generated_commands_compute", 169 },
    { "VK_NV_ray_tracing_linear_swept_spheres", 170 },
    { "VK_NV_linear_color_attachment", 171 },
    { "VK_KHR_shader_maximal_reconvergence", 172 },
    { "VK_EXT_image_compression_control_swapchain", 173 },
    { "VK_QCOM_image_processing", 174 },
    { "VK_EXT_nested_command_buffer", 175 },
    { "VK_EXT_extended_dynamic_state3", 176 },
    { "VK_EXT_subpass_merge_feedback", 177 },
    { "VK_ARM_tensors", 178 },
    { "VK_ARM_tensors", 179 },
    { "VK_EXT_shader_module_identifier", 180 },
    { "VK_NV_optical_flow", 181 },
    { "VK_EXT_legacy_dithering", 182 },
    { "VK_EXT_pipeline_protected_access", 183 },
    { "VK_ANDROID_external_format_resolve", 184 },
    { "VK_KHR_maintenance5", 185 },
    { "VK_AMD_anti_lag", 186 },
    { "VK_AMDX_dense_geometry_format", 187 },
    { "VK_KHR_present_id2", 188 },
    { "VK_KHR_present_wait2", 189 },
    { "VK_KHR_ray_tracing_position_fetch", 190 },
    { "VK_EXT_shader_object", 191 },
    { "VK_KHR_pipeline_binary", 192 },
    { "VK_QCOM_tile_properties", 193 },
    { "VK_SEC_amigo_profiling", 194 },
    { "VK_QCOM_multiview_per_view_viewports", 195 },
    { "VK_NV_ray_tracing_invocation_reorder", 197 },
    { "VK_NV_cooperative_vector", 198 },
    { "VK_NV_extended_sparse_address_space", 199 },
    { "VK_EXT_legacy_vertex_attributes", 200 },
    { "VK_ARM_shader_core_builtins", 201 },
    { "VK_EXT_pipeline_library_group_handles", 202 },
    { "VK_EXT_dynamic_rendering_unused_attachments", 203 },
    { "VK_KHR_internally_synchronized_queues", 204 },
    { "VK_KHR_cooperative_matrix", 205 },
    { "VK_ARM_data_graph", 206 },
    { "VK_QCOM_multiview_per_view_render_areas", 207 },
    { "VK_KHR_video_encode_av1", 208 },
    { "VK_KHR_video_decode_vp9", 209 },
    { "VK_KHR_video_maintenance1", 210 },
    { "VK_NV_per_stage_descriptor_set", 211 },
    { "VK_QCOM_image_processing2", 212 },
    { "VK_QCOM_filter_cubic_weights", 213 },
    { "VK_QCOM_ycbcr_degamma", 214 },
    { "VK_QCOM_filter_cubic_clamp", 215 },
    { "VK_EXT_attachment_feedback_loop_dynamic_state", 216 },
    { "VK_KHR_unified_image_layouts", 217 },
    { "VK_KHR_shader_float_controls2", 218 },
    { "VK_QNX_external_memory_screen_buffer", 219 },
    { "VK_KHR_shader_expect_assume", 220 },
    { "VK_KHR_maintenance6", 221 },
    { "VK_NV_descriptor_pool_overallocation", 222 },
    { "VK_QCOM_tile_memory_heap", 223 },
    { "VK_KHR_copy_memory_indirect", 224 },
    { "VK_KHR_video_encode_intra_refresh", 225 },
    { "VK_KHR_video_encode_quantization_map", 226 },
    { "VK_NV_raw_access_chains", 227 },
    { "VK_KHR_shader_relaxed_extended_instruction", 228 },
    { "VK_NV_command_buffer_inheritance", 229 },
    { "VK_KHR_maintenance7", 230 },
    { "VK_NV_shader_atomic_float16_vector", 231 },
    { "VK_EXT_shader_replicated_composites", 232 },
    { "VK_EXT_shader_float8", 233 },
    { "VK_NV_ray_tracing_validation", 234 },
    { "VK_NV_cluster_acceleration_structure", 235 },
    { "VK_NV_partitioned_acceleration_structure", 236 },
    { "VK_EXT_device_generated_commands", 237 },
    { "VK_KHR_device_fault", 238 },
    { "VK_KHR_maintenance8", 239 },
    { "VK_MESA_image_alignment_control", 240 },
    { "VK_KHR_shader_fma", 241 },
    { "VK_NV_push_constant_bank", 242 },
    { "VK_EXT_ray_tracing_invocation_reorder", 243 },
    { "VK_EXT_depth_clamp_control", 244 },
    { "VK_KHR_maintenance9", 245 },
    { "VK_KHR_video_maintenance2", 246 },
    { "VK_HUAWEI_hdr_vivid", 247 },
    { "VK_NV_cooperative_matrix2", 248 },
    { "VK_ARM_pipeline_opacity_micromap", 249 },
    { "VK_KHR_video_encode_feedback2", 250 },
    { "VK_ARM_performance_counters_by_region", 251 },
    { "VK_ARM_shader_instrumentation", 252 },
    { "VK_EXT_vertex_attribute_robustness", 253 },
    { "VK_ARM_format_pack", 254 },
    { "VK_VALVE_fragment_density_map_layered", 255 },
    { "VK_NV_present_metering", 256 },
    { "VK_EXT_multisampled_render_to_swapchain", 257 },
    { "VK_EXT_zero_initialize_device_memory", 258 },
    { "VK_KHR_opacity_micromap", 259 },
    { "VK_EXT_shader_64bit_indexing", 260 },
    { "VK_EXT_custom_resolve", 261 },
    { "VK_QCOM_data_graph_model", 262 },
    { "VK_KHR_maintenance10", 263 },
    { "VK_ARM_data_graph_optical_flow", 264 },
    { "VK_EXT_shader_long_vector", 265 },
    { "VK_SEC_pipeline_cache_incremental_mode", 266 },
    { "VK_EXT_shader_uniform_buffer_unsized_array", 267 },
    { "VK_NV_compute_occupancy_priority", 268 },
    { "VK_KHR_maintenance11", 269 },
    { "VK_EXT_shader_subgroup_partitioned", 270 },
    { "VK_KHR_extended_flags", 271 },
    { "VK_EXT_shader_ocp_microscaling_types", 272 },
    { "VK_VALVE_shader_mixed_float_dot_product", 273 },
    { "VK_SEC_throttle_hint", 274 },
    { "VK_ARM_data_graph_neural_accelerator_statistics", 275 },
    { "VK_EXT_primitive_restart_index", 276 },
    { "VK_NV_cooperative_matrix_decode_vector", 277 },
};

static const char *const DEVICE_EXTENSIONS[] = {
    "VK_AMDX_dense_geometry_format",
    "VK_AMDX_shader_enqueue",
    "VK_AMD_anti_lag",
    "VK_AMD_buffer_marker",
    "VK_AMD_device_coherent_memory",
    "VK_AMD_display_native_hdr",
    "VK_AMD_draw_indirect_count",
    "VK_AMD_gcn_shader",
    "VK_AMD_gpa_interface",
    "VK_AMD_gpu_shader_half_float",
    "VK_AMD_gpu_shader_int16",
    "VK_AMD_memory_overallocation_behavior",
    "VK_AMD_mixed_attachment_samples",
    "VK_AMD_negative_viewport_height",
    "VK_AMD_pipeline_compiler_control",
    "VK_AMD_rasterization_order",
    "VK_AMD_shader_ballot",
    "VK_AMD_shader_core_properties",
    "VK_AMD_shader_core_properties2",
    "VK_AMD_shader_early_and_late_fragment_tests",
    "VK_AMD_shader_explicit_vertex_parameter",
    "VK_AMD_shader_fragment_mask",
    "VK_AMD_shader_image_load_store_lod",
    "VK_AMD_shader_info",
    "VK_AMD_shader_trinary_minmax",
    "VK_AMD_texture_gather_bias_lod",
    "VK_ANDROID_external_format_resolve",
    "VK_ANDROID_external_memory_android_hardware_buffer",
    "VK_ARM_data_graph",
    "VK_ARM_data_graph_instruction_set_tosa",
    "VK_ARM_data_graph_neural_accelerator_statistics",
    "VK_ARM_data_graph_optical_flow",
    "VK_ARM_format_pack",
    "VK_ARM_performance_counters_by_region",
    "VK_ARM_pipeline_opacity_micromap",
    "VK_ARM_rasterization_order_attachment_access",
    "VK_ARM_render_pass_striped",
    "VK_ARM_scheduling_controls",
    "VK_ARM_shader_core_builtins",
    "VK_ARM_shader_core_properties",
    "VK_ARM_shader_instrumentation",
    "VK_ARM_tensor_controls",
    "VK_ARM_tensors",
    "VK_EXT_4444_formats",
    "VK_EXT_astc_decode_mode",
    "VK_EXT_attachment_feedback_loop_dynamic_state",
    "VK_EXT_attachment_feedback_loop_layout",
    "VK_EXT_blend_operation_advanced",
    "VK_EXT_border_color_swizzle",
    "VK_EXT_buffer_device_address",
    "VK_EXT_calibrated_timestamps",
    "VK_EXT_color_write_enable",
    "VK_EXT_conditional_rendering",
    "VK_EXT_conservative_rasterization",
    "VK_EXT_custom_border_color",
    "VK_EXT_custom_resolve",
    "VK_EXT_debug_marker",
    "VK_EXT_depth_bias_control",
    "VK_EXT_depth_clamp_control",
    "VK_EXT_depth_clamp_zero_one",
    "VK_EXT_depth_clip_control",
    "VK_EXT_depth_clip_enable",
    "VK_EXT_depth_range_unrestricted",
    "VK_EXT_descriptor_buffer",
    "VK_EXT_descriptor_heap",
    "VK_EXT_descriptor_indexing",
    "VK_EXT_device_address_binding_report",
    "VK_EXT_device_fault",
    "VK_EXT_device_generated_commands",
    "VK_EXT_device_memory_report",
    "VK_EXT_discard_rectangles",
    "VK_EXT_display_control",
    "VK_EXT_dynamic_rendering_unused_attachments",
    "VK_EXT_extended_dynamic_state",
    "VK_EXT_extended_dynamic_state2",
    "VK_EXT_extended_dynamic_state3",
    "VK_EXT_external_memory_acquire_unmodified",
    "VK_EXT_external_memory_dma_buf",
    "VK_EXT_external_memory_host",
    "VK_EXT_external_memory_metal",
    "VK_EXT_filter_cubic",
    "VK_EXT_fragment_density_map",
    "VK_EXT_fragment_density_map2",
    "VK_EXT_fragment_density_map_offset",
    "VK_EXT_fragment_shader_interlock",
    "VK_EXT_frame_boundary",
    "VK_EXT_full_screen_exclusive",
    "VK_EXT_global_priority",
    "VK_EXT_global_priority_query",
    "VK_EXT_graphics_pipeline_library",
    "VK_EXT_hdr_metadata",
    "VK_EXT_host_image_copy",
    "VK_EXT_host_query_reset",
    "VK_EXT_image_2d_view_of_3d",
    "VK_EXT_image_compression_control",
    "VK_EXT_image_compression_control_swapchain",
    "VK_EXT_image_drm_format_modifier",
    "VK_EXT_image_robustness",
    "VK_EXT_image_sliced_view_of_3d",
    "VK_EXT_image_view_min_lod",
    "VK_EXT_index_type_uint8",
    "VK_EXT_inline_uniform_block",
    "VK_EXT_legacy_dithering",
    "VK_EXT_legacy_vertex_attributes",
    "VK_EXT_line_rasterization",
    "VK_EXT_load_store_op_none",
    "VK_EXT_map_memory_placed",
    "VK_EXT_memory_budget",
    "VK_EXT_memory_decompression",
    "VK_EXT_memory_priority",
    "VK_EXT_mesh_shader",
    "VK_EXT_metal_objects",
    "VK_EXT_multi_draw",
    "VK_EXT_multisampled_render_to_single_sampled",
    "VK_EXT_multisampled_render_to_swapchain",
    "VK_EXT_mutable_descriptor_type",
    "VK_EXT_nested_command_buffer",
    "VK_EXT_non_seamless_cube_map",
    "VK_EXT_opacity_micromap",
    "VK_EXT_pageable_device_local_memory",
    "VK_EXT_pci_bus_info",
    "VK_EXT_physical_device_drm",
    "VK_EXT_pipeline_creation_cache_control",
    "VK_EXT_pipeline_creation_feedback",
    "VK_EXT_pipeline_library_group_handles",
    "VK_EXT_pipeline_properties",
    "VK_EXT_pipeline_protected_access",
    "VK_EXT_pipeline_robustness",
    "VK_EXT_post_depth_coverage",
    "VK_EXT_present_mode_fifo_latest_ready",
    "VK_EXT_present_timing",
    "VK_EXT_primitive_restart_index",
    "VK_EXT_primitive_topology_list_restart",
    "VK_EXT_primitives_generated_query",
    "VK_EXT_private_data",
    "VK_EXT_provoking_vertex",
    "VK_EXT_queue_family_foreign",
    "VK_EXT_rasterization_order_attachment_access",
    "VK_EXT_ray_tracing_invocation_reorder",
    "VK_EXT_rgba10x6_formats",
    "VK_EXT_robustness2",
    "VK_EXT_sample_locations",
    "VK_EXT_sampler_filter_minmax",
    "VK_EXT_scalar_block_layout",
    "VK_EXT_separate_stencil_usage",
    "VK_EXT_shader_64bit_indexing",
    "VK_EXT_shader_atomic_float",
    "VK_EXT_shader_atomic_float2",
    "VK_EXT_shader_demote_to_helper_invocation",
    "VK_EXT_shader_float8",
    "VK_EXT_shader_image_atomic_int64",
    "VK_EXT_shader_long_vector",
    "VK_EXT_shader_module_identifier",
    "VK_EXT_shader_object",
    "VK_EXT_shader_ocp_microscaling_types",
    "VK_EXT_shader_replicated_composites",
    "VK_EXT_shader_split_barrier",
    "VK_EXT_shader_stencil_export",
    "VK_EXT_shader_subgroup_ballot",
    "VK_EXT_shader_subgroup_partitioned",
    "VK_EXT_shader_subgroup_vote",
    "VK_EXT_shader_tile_image",
    "VK_EXT_shader_uniform_buffer_unsized_array",
    "VK_EXT_shader_viewport_index_layer",
    "VK_EXT_subgroup_size_control",
    "VK_EXT_subpass_merge_feedback",
    "VK_EXT_swapchain_maintenance1",
    "VK_EXT_texel_buffer_alignment",
    "VK_EXT_texture_compression_astc_3d",
    "VK_EXT_texture_compression_astc_hdr",
    "VK_EXT_tooling_info",
    "VK_EXT_transform_feedback",
    "VK_EXT_validation_cache",
    "VK_EXT_vertex_attribute_divisor",
    "VK_EXT_vertex_attribute_robustness",
    "VK_EXT_vertex_input_dynamic_state",
    "VK_EXT_ycbcr_2plane_444_formats",
    "VK_EXT_ycbcr_image_arrays",
    "VK_EXT_zero_initialize_device_memory",
    "VK_FUCHSIA_buffer_collection",
    "VK_FUCHSIA_external_memory",
    "VK_FUCHSIA_external_semaphore",
    "VK_GGP_frame_token",
    "VK_GOOGLE_decorate_string",
    "VK_GOOGLE_display_timing",
    "VK_GOOGLE_hlsl_functionality1",
    "VK_GOOGLE_user_type",
    "VK_HUAWEI_cluster_culling_shader",
    "VK_HUAWEI_hdr_vivid",
    "VK_HUAWEI_invocation_mask",
    "VK_HUAWEI_subpass_shading",
    "VK_IMG_filter_cubic",
    "VK_IMG_filter_linear_2d",
    "VK_IMG_format_pvrtc",
    "VK_IMG_relaxed_line_rasterization",
    "VK_INTEL_performance_query",
    "VK_INTEL_shader_integer_functions2",
    "VK_KHR_16bit_storage",
    "VK_KHR_8bit_storage",
    "VK_KHR_acceleration_structure",
    "VK_KHR_bind_memory2",
    "VK_KHR_buffer_device_address",
    "VK_KHR_calibrated_timestamps",
    "VK_KHR_compute_shader_derivatives",
    "VK_KHR_cooperative_matrix",
    "VK_KHR_copy_commands2",
    "VK_KHR_copy_memory_indirect",
    "VK_KHR_create_renderpass2",
    "VK_KHR_dedicated_allocation",
    "VK_KHR_deferred_host_operations",
    "VK_KHR_depth_clamp_zero_one",
    "VK_KHR_depth_stencil_resolve",
    "VK_KHR_descriptor_update_template",
    "VK_KHR_device_address_commands",
    "VK_KHR_device_fault",
    "VK_KHR_device_group",
    "VK_KHR_display_swapchain",
    "VK_KHR_draw_indirect_count",
    "VK_KHR_driver_properties",
    "VK_KHR_dynamic_rendering",
    "VK_KHR_dynamic_rendering_local_read",
    "VK_KHR_extended_flags",
    "VK_KHR_external_fence",
    "VK_KHR_external_fence_fd",
    "VK_KHR_external_fence_win32",
    "VK_KHR_external_memory",
    "VK_KHR_external_memory_fd",
    "VK_KHR_external_memory_win32",
    "VK_KHR_external_semaphore",
    "VK_KHR_external_semaphore_fd",
    "VK_KHR_external_semaphore_win32",
    "VK_KHR_format_feature_flags2",
    "VK_KHR_fragment_shader_barycentric",
    "VK_KHR_fragment_shading_rate",
    "VK_KHR_get_memory_requirements2",
    "VK_KHR_global_priority",
    "VK_KHR_image_format_list",
    "VK_KHR_imageless_framebuffer",
    "VK_KHR_incremental_present",
    "VK_KHR_index_type_uint8",
    "VK_KHR_internally_synchronized_queues",
    "VK_KHR_line_rasterization",
    "VK_KHR_load_store_op_none",
    "VK_KHR_maintenance1",
    "VK_KHR_maintenance10",
    "VK_KHR_maintenance11",
    "VK_KHR_maintenance2",
    "VK_KHR_maintenance3",
    "VK_KHR_maintenance4",
    "VK_KHR_maintenance5",
    "VK_KHR_maintenance6",
    "VK_KHR_maintenance7",
    "VK_KHR_maintenance8",
    "VK_KHR_maintenance9",
    "VK_KHR_map_memory2",
    "VK_KHR_multiview",
    "VK_KHR_opacity_micromap",
    "VK_KHR_performance_query",
    "VK_KHR_pipeline_binary",
    "VK_KHR_pipeline_executable_properties",
    "VK_KHR_pipeline_library",
    "VK_KHR_portability_subset",
    "VK_KHR_present_id",
    "VK_KHR_present_id2",
    "VK_KHR_present_mode_fifo_latest_ready",
    "VK_KHR_present_wait",
    "VK_KHR_present_wait2",
    "VK_KHR_push_descriptor",
    "VK_KHR_ray_query",
    "VK_KHR_ray_tracing_maintenance1",
    "VK_KHR_ray_tracing_pipeline",
    "VK_KHR_ray_tracing_position_fetch",
    "VK_KHR_relaxed_block_layout",
    "VK_KHR_robustness2",
    "VK_KHR_sampler_mirror_clamp_to_edge",
    "VK_KHR_sampler_ycbcr_conversion",
    "VK_KHR_separate_depth_stencil_layouts",
    "VK_KHR_shader_abort",
    "VK_KHR_shader_atomic_int64",
    "VK_KHR_shader_bfloat16",
    "VK_KHR_shader_clock",
    "VK_KHR_shader_constant_data",
    "VK_KHR_shader_draw_parameters",
    "VK_KHR_shader_expect_assume",
    "VK_KHR_shader_float16_int8",
    "VK_KHR_shader_float_controls",
    "VK_KHR_shader_float_controls2",
    "VK_KHR_shader_fma",
    "VK_KHR_shader_integer_dot_product",
    "VK_KHR_shader_maximal_reconvergence",
    "VK_KHR_shader_non_semantic_info",
    "VK_KHR_shader_quad_control",
    "VK_KHR_shader_relaxed_extended_instruction",
    "VK_KHR_shader_subgroup_extended_types",
    "VK_KHR_shader_subgroup_rotate",
    "VK_KHR_shader_subgroup_uniform_control_flow",
    "VK_KHR_shader_terminate_invocation",
    "VK_KHR_shader_untyped_pointers",
    "VK_KHR_shared_presentable_image",
    "VK_KHR_spirv_1_4",
    "VK_KHR_storage_buffer_storage_class",
    "VK_KHR_swapchain",
    "VK_KHR_swapchain_maintenance1",
    "VK_KHR_swapchain_mutable_format",
    "VK_KHR_synchronization2",
    "VK_KHR_timeline_semaphore",
    "VK_KHR_unified_image_layouts",
    "VK_KHR_uniform_buffer_standard_layout",
    "VK_KHR_variable_pointers",
    "VK_KHR_vertex_attribute_divisor",
    "VK_KHR_video_decode_av1",
    "VK_KHR_video_decode_h264",
    "VK_KHR_video_decode_h265",
    "VK_KHR_video_decode_queue",
    "VK_KHR_video_decode_vp9",
    "VK_KHR_video_encode_av1",
    "VK_KHR_video_encode_feedback2",
    "VK_KHR_video_encode_h264",
    "VK_KHR_video_encode_h265",
    "VK_KHR_video_encode_intra_refresh",
    "VK_KHR_video_encode_quantization_map",
    "VK_KHR_video_encode_queue",
    "VK_KHR_video_maintenance1",
    "VK_KHR_video_maintenance2",
    "VK_KHR_video_queue",
    "VK_KHR_vulkan_memory_model",
    "VK_KHR_win32_keyed_mutex",
    "VK_KHR_workgroup_memory_explicit_layout",
    "VK_KHR_zero_initialize_workgroup_memory",
    "VK_MESA_image_alignment_control",
    "VK_MSFT_layered_driver",
    "VK_NVX_binary_import",
    "VK_NVX_image_view_handle",
    "VK_NVX_multiview_per_view_attributes",
    "VK_NV_acquire_winrt_display",
    "VK_NV_clip_space_w_scaling",
    "VK_NV_cluster_acceleration_structure",
    "VK_NV_command_buffer_inheritance",
    "VK_NV_compute_occupancy_priority",
    "VK_NV_compute_shader_derivatives",
    "VK_NV_cooperative_matrix",
    "VK_NV_cooperative_matrix2",
    "VK_NV_cooperative_matrix_decode_vector",
    "VK_NV_cooperative_vector",
    "VK_NV_copy_memory_indirect",
    "VK_NV_corner_sampled_image",
    "VK_NV_coverage_reduction_mode",
    "VK_NV_cuda_kernel_launch",
    "VK_NV_dedicated_allocation",
    "VK_NV_dedicated_allocation_image_aliasing",
    "VK_NV_descriptor_pool_overallocation",
    "VK_NV_device_diagnostic_checkpoints",
    "VK_NV_device_diagnostics_config",
    "VK_NV_device_generated_commands",
    "VK_NV_device_generated_commands_compute",
    "VK_NV_displacement_micromap",
    "VK_NV_extended_sparse_address_space",
    "VK_NV_external_compute_queue",
    "VK_NV_external_memory",
    "VK_NV_external_memory_rdma",
    "VK_NV_external_memory_win32",
    "VK_NV_fill_rectangle",
    "VK_NV_fragment_coverage_to_color",
    "VK_NV_fragment_shader_barycentric",
    "VK_NV_fragment_shading_rate_enums",
    "VK_NV_framebuffer_mixed_samples",
    "VK_NV_geometry_shader_passthrough",
    "VK_NV_glsl_shader",
    "VK_NV_inherited_viewport_scissor",
    "VK_NV_linear_color_attachment",
    "VK_NV_low_latency",
    "VK_NV_low_latency2",
    "VK_NV_memory_decompression",
    "VK_NV_mesh_shader",
    "VK_NV_optical_flow",
    "VK_NV_partitioned_acceleration_structure",
    "VK_NV_per_stage_descriptor_set",
    "VK_NV_present_barrier",
    "VK_NV_present_metering",
    "VK_NV_push_constant_bank",
    "VK_NV_raw_access_chains",
    "VK_NV_ray_tracing",
    "VK_NV_ray_tracing_invocation_reorder",
    "VK_NV_ray_tracing_linear_swept_spheres",
    "VK_NV_ray_tracing_motion_blur",
    "VK_NV_ray_tracing_validation",
    "VK_NV_representative_fragment_test",
    "VK_NV_sample_mask_override_coverage",
    "VK_NV_scissor_exclusive",
    "VK_NV_shader_atomic_float16_vector",
    "VK_NV_shader_image_footprint",
    "VK_NV_shader_sm_builtins",
    "VK_NV_shader_subgroup_partitioned",
    "VK_NV_shading_rate_image",
    "VK_NV_viewport_array2",
    "VK_NV_viewport_swizzle",
    "VK_NV_win32_keyed_mutex",
    "VK_OHOS_external_memory",
    "VK_QCOM_cooperative_matrix_conversion",
    "VK_QCOM_data_graph_model",
    "VK_QCOM_elapsed_timer_query",
    "VK_QCOM_filter_cubic_clamp",
    "VK_QCOM_filter_cubic_weights",
    "VK_QCOM_fragment_density_map_offset",
    "VK_QCOM_image_processing",
    "VK_QCOM_image_processing2",
    "VK_QCOM_image_processing3",
    "VK_QCOM_multiview_per_view_render_areas",
    "VK_QCOM_multiview_per_view_viewports",
    "VK_QCOM_queue_perf_hint",
    "VK_QCOM_render_pass_shader_resolve",
    "VK_QCOM_render_pass_store_ops",
    "VK_QCOM_render_pass_transform",
    "VK_QCOM_rotated_copy_commands",
    "VK_QCOM_shader_multiple_wait_queues",
    "VK_QCOM_tile_memory_heap",
    "VK_QCOM_tile_properties",
    "VK_QCOM_tile_shading",
    "VK_QCOM_ycbcr_degamma",
    "VK_QNX_external_memory_screen_buffer",
    "VK_SEC_amigo_profiling",
    "VK_SEC_pipeline_cache_incremental_mode",
    "VK_SEC_throttle_hint",
    "VK_VALVE_descriptor_set_host_mapping",
    "VK_VALVE_fragment_density_map_layered",
    "VK_VALVE_mutable_descriptor_type",
    "VK_VALVE_shader_mixed_float_dot_product",
    "VK_VALVE_video_encode_rgb_conversion",
};
