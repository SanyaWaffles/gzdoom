
#include "vk_renderpass.h"
#include "vulkan/shaders/vk_shader.h"
#include "vulkan/system/vk_builders.h"
#include "vulkan/system/vk_framebuffer.h"
#include "vulkan/system/vk_buffers.h"
#include "hwrenderer/data/flatvertices.h"
#include "hwrenderer/scene/hw_viewpointuniforms.h"
#include "rendering/2d/v_2ddrawer.h"

VkRenderPassManager::VkRenderPassManager()
{
	CreateDynamicSetLayout();
	CreateTextureSetLayout();
	CreatePipelineLayout();
	CreateDescriptorPool();
	CreateDynamicSet();
}

void VkRenderPassManager::BeginFrame()
{
	if (!SceneColor || SceneColor->width != SCREENWIDTH || SceneColor->height != SCREENHEIGHT)
	{
		auto fb = GetVulkanFrameBuffer();

		RenderPassSetup.clear();
		SceneColorView.reset();
		SceneDepthStencilView.reset();
		SceneDepthView.reset();
		SceneColor.reset();
		SceneDepthStencil.reset();

		ImageBuilder builder;
		builder.setSize(SCREENWIDTH, SCREENHEIGHT);
		builder.setFormat(VK_FORMAT_R16G16B16A16_SFLOAT);
		builder.setUsage(VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
		SceneColor = builder.create(fb->device);

		builder.setFormat(VK_FORMAT_D24_UNORM_S8_UINT);
		builder.setUsage(VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
		SceneDepthStencil = builder.create(fb->device);

		ImageViewBuilder viewbuilder;
		viewbuilder.setImage(SceneColor.get(), VK_FORMAT_R16G16B16A16_SFLOAT);
		SceneColorView = viewbuilder.create(fb->device);

		viewbuilder.setImage(SceneDepthStencil.get(), VK_FORMAT_D24_UNORM_S8_UINT, VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT);
		SceneDepthStencilView = viewbuilder.create(fb->device);

		viewbuilder.setImage(SceneDepthStencil.get(), VK_FORMAT_D24_UNORM_S8_UINT, VK_IMAGE_ASPECT_DEPTH_BIT);
		SceneDepthView = viewbuilder.create(fb->device);
	}
}

VkRenderPassSetup *VkRenderPassManager::GetRenderPass(const VkRenderPassKey &key)
{
	auto &item = RenderPassSetup[key];
	if (!item)
		item.reset(new VkRenderPassSetup(key));
	return item.get();
}

void VkRenderPassManager::CreateDynamicSetLayout()
{
	DescriptorSetLayoutBuilder builder;
	builder.addBinding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT);
	builder.addBinding(1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1, VK_SHADER_STAGE_FRAGMENT_BIT);
	builder.addBinding(2, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT);
	builder.addBinding(3, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT);
	builder.addBinding(4, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT);
	DynamicSetLayout = builder.create(GetVulkanFrameBuffer()->device);
}

void VkRenderPassManager::CreateTextureSetLayout()
{
	DescriptorSetLayoutBuilder builder;
	builder.addBinding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT);
	/*
	for (int i = 0; i < 6; i++)
	{
		builder.addBinding(i, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT);
	}
	builder.addBinding(16, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT);
	*/
	TextureSetLayout = builder.create(GetVulkanFrameBuffer()->device);
}

void VkRenderPassManager::CreatePipelineLayout()
{
	PipelineLayoutBuilder builder;
	builder.addSetLayout(DynamicSetLayout.get());
	builder.addSetLayout(TextureSetLayout.get());
	builder.addPushConstantRange(VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PushConstants));
	PipelineLayout = builder.create(GetVulkanFrameBuffer()->device);
}

void VkRenderPassManager::CreateDescriptorPool()
{
	DescriptorPoolBuilder builder;
	builder.addPoolSize(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 4);
	builder.addPoolSize(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1);
	builder.addPoolSize(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 512 * 6);
	builder.setMaxSets(512);
	DescriptorPool = builder.create(GetVulkanFrameBuffer()->device);
}

void VkRenderPassManager::CreateDynamicSet()
{
	DynamicSet = DescriptorPool->allocate(DynamicSetLayout.get());

	auto fb = GetVulkanFrameBuffer();
	WriteDescriptors update;
	update.addBuffer(DynamicSet.get(), 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, fb->ViewpointUBO->mBuffer.get(), 0, sizeof(HWViewpointUniforms));
	update.addBuffer(DynamicSet.get(), 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, fb->LightBufferSSO->mBuffer.get(), 0, 4096);
	update.addBuffer(DynamicSet.get(), 2, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, fb->MatricesUBO->mBuffer.get(), 0, sizeof(MatricesUBO));
	update.addBuffer(DynamicSet.get(), 3, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, fb->ColorsUBO->mBuffer.get(), 0, sizeof(ColorsUBO));
	update.addBuffer(DynamicSet.get(), 4, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, fb->GlowingWallsUBO->mBuffer.get(), 0, sizeof(GlowingWallsUBO));
	update.updateSets(fb->device);
}

/////////////////////////////////////////////////////////////////////////////

VkRenderPassSetup::VkRenderPassSetup(const VkRenderPassKey &key)
{
	CreateRenderPass();
	CreatePipeline(key);
	CreateFramebuffer();
}

void VkRenderPassSetup::CreateRenderPass()
{
	RenderPassBuilder builder;
	builder.addRgba16fAttachment(false, VK_IMAGE_LAYOUT_GENERAL);
	builder.addSubpass();
	builder.addSubpassColorAttachmentRef(0, VK_IMAGE_LAYOUT_GENERAL);
	builder.addExternalSubpassDependency();
	RenderPass = builder.create(GetVulkanFrameBuffer()->device);
}

void VkRenderPassSetup::CreatePipeline(const VkRenderPassKey &key)
{
	auto fb = GetVulkanFrameBuffer();
	GraphicsPipelineBuilder builder;
	builder.addVertexShader(fb->GetShaderManager()->vert.get());
	builder.addFragmentShader(fb->GetShaderManager()->frag.get());

	builder.addVertexBufferBinding(0, sizeof(F2DDrawer::TwoDVertex));
	builder.addVertexAttribute(0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(F2DDrawer::TwoDVertex, x));
	builder.addVertexAttribute(1, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(F2DDrawer::TwoDVertex, u));
	builder.addVertexAttribute(2, 0, VK_FORMAT_R8G8B8A8_UNORM, offsetof(F2DDrawer::TwoDVertex, color0));
	builder.addVertexAttribute(3, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(F2DDrawer::TwoDVertex, x));
	builder.addVertexAttribute(4, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(F2DDrawer::TwoDVertex, x));
	builder.addVertexAttribute(5, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(F2DDrawer::TwoDVertex, x));

#if 0
		builder.addVertexBufferBinding(0, sizeof(FFlatVertex));
	builder.addVertexAttribute(0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(FFlatVertex, x));
	builder.addVertexAttribute(1, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(FFlatVertex, u));
	// To do: not all vertex formats has all the data..
	builder.addVertexAttribute(2, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(FFlatVertex, x));
	builder.addVertexAttribute(3, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(FFlatVertex, x));
	builder.addVertexAttribute(4, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(FFlatVertex, x));
	builder.addVertexAttribute(5, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(FFlatVertex, x));
#endif

	builder.addDynamicState(VK_DYNAMIC_STATE_VIEWPORT);
	builder.addDynamicState(VK_DYNAMIC_STATE_SCISSOR);
	// builder.addDynamicState(VK_DYNAMIC_STATE_LINE_WIDTH);
	// builder.addDynamicState(VK_DYNAMIC_STATE_DEPTH_BIAS);
	// builder.addDynamicState(VK_DYNAMIC_STATE_BLEND_CONSTANTS);
	// builder.addDynamicState(VK_DYNAMIC_STATE_DEPTH_BOUNDS);
	// builder.addDynamicState(VK_DYNAMIC_STATE_STENCIL_COMPARE_MASK);
	// builder.addDynamicState(VK_DYNAMIC_STATE_STENCIL_WRITE_MASK);
	// builder.addDynamicState(VK_DYNAMIC_STATE_STENCIL_REFERENCE);

	builder.setViewport(0.0f, 0.0f, (float)SCREENWIDTH, (float)SCREENHEIGHT);
	builder.setScissor(0, 0, SCREENWIDTH, SCREENHEIGHT);

	static const int blendstyles[] = {
		VK_BLEND_FACTOR_ZERO,
		VK_BLEND_FACTOR_ONE,
		VK_BLEND_FACTOR_SRC_ALPHA,
		VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
		VK_BLEND_FACTOR_SRC_COLOR,
		VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR,
		VK_BLEND_FACTOR_DST_COLOR,
		VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR,
	};

	static const int renderops[] = {
		0, VK_BLEND_OP_ADD, VK_BLEND_OP_SUBTRACT, VK_BLEND_OP_REVERSE_SUBTRACT, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1
	};

	int srcblend = blendstyles[key.RenderStyle.SrcAlpha%STYLEALPHA_MAX];
	int dstblend = blendstyles[key.RenderStyle.DestAlpha%STYLEALPHA_MAX];
	int blendequation = renderops[key.RenderStyle.BlendOp & 15];

	if (blendequation == -1)	// This was a fuzz style.
	{
		srcblend = VK_BLEND_FACTOR_DST_COLOR;
		dstblend = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
		blendequation = VK_BLEND_OP_ADD;
	}

	builder.setBlendMode((VkBlendOp)blendequation, (VkBlendFactor)srcblend, (VkBlendFactor)dstblend);

	builder.setLayout(fb->GetRenderPassManager()->PipelineLayout.get());
	builder.setRenderPass(RenderPass.get());
	Pipeline = builder.create(fb->device);
}

void VkRenderPassSetup::CreateFramebuffer()
{
	auto fb = GetVulkanFrameBuffer();
	FramebufferBuilder builder;
	builder.setRenderPass(RenderPass.get());
	builder.setSize(SCREENWIDTH, SCREENHEIGHT);
	builder.addAttachment(fb->GetRenderPassManager()->SceneColorView.get());
	Framebuffer = builder.create(GetVulkanFrameBuffer()->device);
}