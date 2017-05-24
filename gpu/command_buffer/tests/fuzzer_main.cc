// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include <stddef.h>
#include <stdint.h>

#include <memory>
#include <vector>

#include "base/at_exit.h"
#include "base/bind.h"
#include "base/command_line.h"
#include "base/logging.h"
#include "base/memory/ptr_util.h"
#include "base/memory/ref_counted.h"
#include "build/build_config.h"
#include "gpu/command_buffer/common/constants.h"
#include "gpu/command_buffer/common/gles2_cmd_utils.h"
#include "gpu/command_buffer/common/sync_token.h"
#include "gpu/command_buffer/service/buffer_manager.h"
#include "gpu/command_buffer/service/command_buffer_service.h"
#include "gpu/command_buffer/service/command_executor.h"
#include "gpu/command_buffer/service/context_group.h"
#include "gpu/command_buffer/service/gles2_cmd_decoder.h"
#include "gpu/command_buffer/service/gpu_switches.h"
#include "gpu/command_buffer/service/logger.h"
#include "gpu/command_buffer/service/mailbox_manager_impl.h"
#include "gpu/command_buffer/service/service_discardable_manager.h"
#include "gpu/command_buffer/service/sync_point_manager.h"
#include "gpu/command_buffer/service/transfer_buffer_manager.h"
#include "ui/gfx/geometry/size.h"
#include "ui/gl/gl_context.h"
#include "ui/gl/gl_context_egl.h"
#include "ui/gl/gl_context_stub.h"
#include "ui/gl/gl_image_ref_counted_memory.h"
#include "ui/gl/gl_share_group.h"
#include "ui/gl/gl_stub_api.h"
#include "ui/gl/gl_surface.h"
#include "ui/gl/gl_surface_egl.h"
#include "ui/gl/gl_surface_stub.h"
#include "ui/gl/init/gl_factory.h"
#include "ui/gl/test/gl_surface_test_support.h"

namespace gpu {
namespace {

const size_t kCommandBufferSize = 16384;
const size_t kTransferBufferSize = 16384;
const size_t kSmallTransferBufferSize = 16;
const size_t kTinyTransferBufferSize = 3;

#if !defined(GPU_FUZZER_USE_ANGLE)
static const char kExtensions[] =
    "GL_AMD_compressed_ATC_texture "
    "GL_ANGLE_texture_compression_dxt3 "
    "GL_ANGLE_texture_compression_dxt5 "
    "GL_ANGLE_texture_usage "
    "GL_APPLE_ycbcr_422 "
    "GL_ARB_texture_rectangle "
    "GL_EXT_blend_func_extended "
    "GL_EXT_color_buffer_float "
    "GL_EXT_disjoint_timer_query "
    "GL_EXT_draw_buffers "
    "GL_EXT_frag_depth "
    "GL_EXT_multisample_compatibility "
    "GL_EXT_multisampled_render_to_texture "
    "GL_EXT_occlusion_query_boolean "
    "GL_EXT_read_format_bgra "
    "GL_EXT_shader_texture_lod "
    "GL_EXT_texture_compression_s3tc "
    "GL_EXT_texture_filter_anisotropic "
    "GL_IMG_texture_compression_pvrtc "
    "GL_KHR_blend_equation_advanced "
    "GL_KHR_blend_equation_advanced_coherent "
    "GL_KHR_texture_compression_astc_ldr "
    "GL_NV_EGL_stream_consumer_external "
    "GL_NV_framebuffer_mixed_samples "
    "GL_NV_path_rendering "
    "GL_OES_compressed_ETC1_RGB8_texture "
    "GL_OES_depth24 "
    "GL_OES_EGL_image_external "
    "GL_OES_rgb8_rgba8 "
    "GL_OES_texture_float "
    "GL_OES_texture_float_linear "
    "GL_OES_texture_half_float "
    "GL_OES_texture_half_float_linear";
#endif

class CommandBufferSetup {
 public:
  CommandBufferSetup()
      : atexit_manager_(),
        sync_point_manager_(new SyncPointManager()),
        sync_point_order_data_(sync_point_manager_->CreateSyncPointOrderData()),
        mailbox_manager_(new gles2::MailboxManagerImpl),
        share_group_(new gl::GLShareGroup),
        command_buffer_id_(CommandBufferId::FromUnsafeValue(1)) {
    logging::SetMinLogLevel(logging::LOG_FATAL);
    base::CommandLine::Init(0, NULL);

    auto* command_line = base::CommandLine::ForCurrentProcess();
    ALLOW_UNUSED_LOCAL(command_line);

#if defined(GPU_FUZZER_USE_PASSTHROUGH_CMD_DECODER)
    command_line->AppendSwitch(switches::kUsePassthroughCmdDecoder);
    gpu_preferences_.use_passthrough_cmd_decoder = true;
    recreate_context_ = true;
#endif

#if defined(GPU_FUZZER_USE_ANGLE)
    command_line->AppendSwitchASCII(switches::kUseGL,
                                    gl::kGLImplementationANGLEName);
    command_line->AppendSwitchASCII(switches::kUseANGLE,
                                    gl::kANGLEImplementationNullName);
    gl::init::InitializeGLOneOffImplementation(gl::kGLImplementationEGLGLES2,
                                               false, false, false);

    surface_ = new gl::PbufferGLSurfaceEGL(gfx::Size());
    surface_->Initialize();
    if (!recreate_context_) {
      InitContext();
    }
#else
    surface_ = new gl::GLSurfaceStub;
    InitContext();
    gl::GLSurfaceTestSupport::InitializeOneOffWithMockBindings();
#endif

    sync_point_client_state_ = sync_point_manager_->CreateSyncPointClientState(
        CommandBufferNamespace::IN_PROCESS, command_buffer_id_,
        sync_point_order_data_->sequence_id());

    translator_cache_ = new gles2::ShaderTranslatorCache(gpu_preferences_);
    completeness_cache_ = new gles2::FramebufferCompletenessCache;
  }

  void InitDecoder() {
    if (recreate_context_) {
      InitContext();
    }

    context_->MakeCurrent(surface_.get());
    scoped_refptr<gles2::FeatureInfo> feature_info =
        new gles2::FeatureInfo();
    scoped_refptr<gles2::ContextGroup> context_group = new gles2::ContextGroup(
        gpu_preferences_, mailbox_manager_.get(), nullptr, translator_cache_,
        completeness_cache_, feature_info, true /* bind_generates_resource */,
        nullptr /* image_factory */, nullptr /* progress_reporter */,
        GpuFeatureInfo(), &discardable_manager_);
    command_buffer_.reset(
        new CommandBufferService(context_group->transfer_buffer_manager()));
    command_buffer_->SetPutOffsetChangeCallback(
        base::Bind(&CommandBufferSetup::PumpCommands, base::Unretained(this)));
    command_buffer_->SetGetBufferChangeCallback(base::Bind(
        &CommandBufferSetup::GetBufferChanged, base::Unretained(this)));
    InitializeInitialCommandBuffer();

    decoder_.reset(gles2::GLES2Decoder::Create(context_group.get()));
    executor_.reset(new CommandExecutor(command_buffer_.get(), decoder_.get(),
                                        decoder_.get()));
    decoder_->set_engine(executor_.get());
    decoder_->SetFenceSyncReleaseCallback(base::Bind(
        &CommandBufferSetup::OnFenceSyncRelease, base::Unretained(this)));
    decoder_->SetWaitSyncTokenCallback(base::Bind(
        &CommandBufferSetup::OnWaitSyncToken, base::Unretained(this)));
    decoder_->GetLogger()->set_log_synthesized_gl_errors(false);

    gles2::ContextCreationAttribHelper attrib_helper;
    attrib_helper.offscreen_framebuffer_size = gfx::Size(16, 16);
    attrib_helper.red_size = 8;
    attrib_helper.green_size = 8;
    attrib_helper.blue_size = 8;
    attrib_helper.alpha_size = 8;
    attrib_helper.depth_size = 0;
    attrib_helper.stencil_size = 0;
    attrib_helper.context_type = gles2::CONTEXT_TYPE_OPENGLES3;

    bool result =
        decoder_->Initialize(surface_.get(), context_.get(), true,
                             gles2::DisallowedFeatures(), attrib_helper);
    CHECK(result);
    decoder_->set_max_bucket_size(8 << 20);
    context_group->buffer_manager()->set_max_buffer_size(8 << 20);
    if (!vertex_translator_) {
      // Keep a reference to the translators, which keeps them in the cache even
      // after the decoder is reset. They are expensive to initialize, but they
      // don't keep state.
      vertex_translator_ = decoder_->GetTranslator(GL_VERTEX_SHADER);
      fragment_translator_ = decoder_->GetTranslator(GL_FRAGMENT_SHADER);
    }
  }

  void ResetDecoder() {
    decoder_->Destroy(true);
    decoder_.reset();
    if (recreate_context_) {
      context_->ReleaseCurrent(nullptr);
      context_ = nullptr;
    }
    command_buffer_.reset();
  }

  ~CommandBufferSetup() {
    if (sync_point_order_data_) {
      sync_point_order_data_->Destroy();
      sync_point_order_data_ = nullptr;
    }
    if (sync_point_client_state_) {
      sync_point_client_state_->Destroy();
      sync_point_client_state_ = nullptr;
    }
  }

  void RunCommandBuffer(const uint8_t* data, size_t size) {
    // The commands are flushed at a uint32_t granularity. If the data is not
    // a full command, we zero-pad it.
    size_t padded_size = (size + 3) & ~3;
    // crbug.com/638836 The -max_len argument is sometimes not respected, so the
    // fuzzer may give us too much data. Bail ASAP in that case.
    if (padded_size > kCommandBufferSize)
      return;

    InitDecoder();
    size_t buffer_size = buffer_->size();
    CHECK_LE(padded_size, buffer_size);
    command_buffer_->SetGetBuffer(buffer_id_);
    auto* memory = static_cast<char*>(buffer_->memory());
    memcpy(memory, data, size);
    if (size < buffer_size)
      memset(memory + size, 0, buffer_size - size);
    command_buffer_->Flush(padded_size / 4);
    ResetDecoder();
  }

 private:
  void PumpCommands() {
    if (!decoder_->MakeCurrent()) {
      command_buffer_->SetContextLostReason(decoder_->GetContextLostReason());
      command_buffer_->SetParseError(::gpu::error::kLostContext);
      return;
    }

    uint32_t order_num =
        sync_point_order_data_->GenerateUnprocessedOrderNumber();
    sync_point_order_data_->BeginProcessingOrderNumber(order_num);

    executor_->PutChanged();

    sync_point_order_data_->FinishProcessingOrderNumber(order_num);
  }

  bool GetBufferChanged(int32_t transfer_buffer_id) {
    return executor_->SetGetBuffer(transfer_buffer_id);
  }

  void OnFenceSyncRelease(uint64_t release) {
    CHECK(sync_point_client_state_);
    sync_point_client_state_->ReleaseFenceSync(release);
  }

  bool OnWaitSyncToken(const SyncToken& sync_token) {
    CHECK(sync_point_manager_);
    if (sync_point_manager_->IsSyncTokenReleased(sync_token))
      return false;
    executor_->SetScheduled(false);
    return true;
  }

  void CreateTransferBuffer(size_t size, int32_t id) {
    scoped_refptr<Buffer> buffer =
        command_buffer_->CreateTransferBufferWithId(size, id);
    memset(buffer->memory(), 0, size);
  }

  void InitializeInitialCommandBuffer() {
    buffer_id_ = 1;
    buffer_ = command_buffer_->CreateTransferBufferWithId(kCommandBufferSize,
                                                          buffer_id_);
    CHECK(buffer_);
    // Create some transfer buffers. This is somewhat arbitrary, but having a
    // reasonably sized buffer in slot 4 allows us to prime the corpus with data
    // extracted from unit tests.
    CreateTransferBuffer(kTransferBufferSize, 2);
    CreateTransferBuffer(kSmallTransferBufferSize, 3);
    CreateTransferBuffer(kTransferBufferSize, 4);
    CreateTransferBuffer(kTinyTransferBufferSize, 5);
  }

  void InitContext() {
#if defined(GPU_FUZZER_USE_ANGLE)
    context_ = new gl::GLContextEGL(share_group_.get());
    context_->Initialize(surface_.get(), gl::GLContextAttribs());
#else
    scoped_refptr<gl::GLContextStub> context_stub =
        new gl::GLContextStub(share_group_.get());
    context_stub->SetGLVersionString("OpenGL ES 3.1");
    context_stub->SetExtensionsString(kExtensions);
    context_stub->SetUseStubApi(true);
    context_ = context_stub;
#endif
  }

  base::AtExitManager atexit_manager_;

  GpuPreferences gpu_preferences_;

  std::unique_ptr<SyncPointManager> sync_point_manager_;
  scoped_refptr<SyncPointOrderData> sync_point_order_data_;
  scoped_refptr<SyncPointClientState> sync_point_client_state_;
  scoped_refptr<gles2::MailboxManager> mailbox_manager_;
  scoped_refptr<gl::GLShareGroup> share_group_;
  const gpu::CommandBufferId command_buffer_id_;
  ServiceDiscardableManager discardable_manager_;

  bool recreate_context_ = false;
  scoped_refptr<gl::GLSurface> surface_;
  scoped_refptr<gl::GLContext> context_;

  scoped_refptr<gles2::ShaderTranslatorCache> translator_cache_;
  scoped_refptr<gles2::FramebufferCompletenessCache> completeness_cache_;

  std::unique_ptr<CommandBufferService> command_buffer_;

  std::unique_ptr<gles2::GLES2Decoder> decoder_;
  std::unique_ptr<CommandExecutor> executor_;

  scoped_refptr<gles2::ShaderTranslatorInterface> vertex_translator_;
  scoped_refptr<gles2::ShaderTranslatorInterface> fragment_translator_;

  scoped_refptr<Buffer> buffer_;
  int32_t buffer_id_ = 0;
};

// Intentionally leaked because asan tries to exit cleanly after a crash, but
// the decoder is usually in a bad state at that point.
// We need to load ANGLE libraries before the fuzzer infrastructure starts,
// because it gets confused about new coverage counters being dynamically
// registered, causing crashes.
CommandBufferSetup* g_setup = new CommandBufferSetup();

}  // anonymous namespace
}  // namespace gpu

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  gpu::g_setup->RunCommandBuffer(data, size);
  return 0;
}
