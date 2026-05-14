project('imgui', 'cpp',
  version : '1.91.6',
  default_options : ['cpp_std=c++17'])

inc = include_directories('.')

imgui_sources = files(
  'imgui.cpp',
  'imgui_demo.cpp',
  'imgui_draw.cpp',
  'imgui_tables.cpp',
  'imgui_widgets.cpp',
  'backends/imgui_impl_glfw.cpp',
  'backends/imgui_impl_opengl3.cpp',
)

glfw_dep = dependency('glfw3')
gl_dep   = dependency('gl')
# imgui_impl_glfw references dlclose (Wayland/X11 probing). On glibc <2.34
# this lives in a separate libdl; on newer glibc the symbol is in libc and
# find_library returns an empty stub, so the link line stays portable.
dl_dep   = meson.get_compiler('cpp').find_library('dl', required : false)

imgui_lib = static_library('imgui',
  imgui_sources,
  include_directories : [inc, include_directories('backends')],
  dependencies : [glfw_dep, gl_dep, dl_dep],
)

imgui_dep = declare_dependency(
  include_directories : [inc, include_directories('backends')],
  link_with : imgui_lib,
  dependencies : [dl_dep],
)
