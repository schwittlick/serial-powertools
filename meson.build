project('serial-powertools', 'cpp',
  version : '0.1.0',
  default_options : ['cpp_std=c++17', 'warning_level=2'])

# ── Dependencies ──────────────────────────────────────────────────────────────
glfw_dep    = dependency('glfw3')
gl_dep      = dependency('gl')
serial_dep  = dependency('libserialport')
threads_dep = dependency('threads')

# ImGui via wrap (subprojects/imgui.wrap — same revision=docking as hpgl-viewer)
imgui_proj = subproject('imgui', default_options : ['default_library=static'])
imgui_dep  = imgui_proj.get_variable('imgui_dep')

inc = include_directories('src')

# ── Pure-logic libs (no UI/serial deps — easy to unit-test) ───────────────────
tokenizer_lib = static_library('plt_tokenizer',
  sources : ['src/hpgl/Tokenizer.cpp'],
  include_directories : inc)

memcfg_lib = static_library('plt_memcfg',
  sources : ['src/hpgl/Hp7550aMemoryConfig.cpp'],
  include_directories : inc)

eta_lib = static_library('plt_eta',
  sources : ['src/app/EtaEstimator.cpp'],
  include_directories : inc)

# ── Serial / plotter / discovery ──────────────────────────────────────────────
hpgl_lib = static_library('plt_hpgl',
  sources : ['src/hpgl/SerialIo.cpp', 'src/hpgl/HpglPlotter.cpp'],
  link_with : [tokenizer_lib, memcfg_lib],
  include_directories : inc,
  dependencies : [serial_dep, threads_dep])

discovery_lib = static_library('plt_discovery',
  sources : ['src/discovery/PortDiscovery.cpp'],
  link_with : [hpgl_lib],
  include_directories : inc,
  dependencies : [serial_dep, threads_dep])

sender_lib = static_library('plt_sender',
  sources : ['src/sender/AsyncSerialSender.cpp'],
  link_with : [hpgl_lib, tokenizer_lib],
  include_directories : inc,
  dependencies : [threads_dep])

reporter_lib = static_library('plt_reporter',
  sources : ['src/reporter/ProgressReporter.cpp'],
  include_directories : inc,
  dependencies : [threads_dep])

app_lib = static_library('plt_app',
  sources : ['src/app/AppState.cpp'],
  link_with : [hpgl_lib, sender_lib, discovery_lib, reporter_lib, eta_lib, tokenizer_lib],
  include_directories : inc,
  dependencies : [serial_dep, threads_dep])

# ── Executable ────────────────────────────────────────────────────────────────
executable('serial-powertools',
  sources : ['src/main.cpp', 'src/ui/Ui.cpp'],
  link_with : [app_lib, hpgl_lib, sender_lib, discovery_lib, reporter_lib, eta_lib, tokenizer_lib, memcfg_lib],
  include_directories : inc,
  dependencies : [glfw_dep, gl_dep, imgui_dep, serial_dep, threads_dep],
  install : true)

if get_option('build_tests')
  subdir('tests')
endif
