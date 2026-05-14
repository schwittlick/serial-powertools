tokenizer_test = executable('test_tokenizer',
  sources : ['test_tokenizer.cpp'],
  link_with : [tokenizer_lib],
  include_directories : inc)
test('tokenizer', tokenizer_test)

concat_test = executable('test_concat_commands',
  sources : ['test_concat_commands.cpp'],
  link_with : [tokenizer_lib],
  include_directories : inc)
test('concat_commands', concat_test)

memcfg_test = executable('test_memory_config',
  sources : ['test_memory_config.cpp'],
  link_with : [memcfg_lib],
  include_directories : inc)
test('memory_config', memcfg_test)

eta_test = executable('test_eta_estimator',
  sources : ['test_eta_estimator.cpp'],
  link_with : [eta_lib],
  include_directories : inc)
test('eta_estimator', eta_test)

reporter_test = executable('test_progress_reporter',
  sources : ['test_progress_reporter.cpp'],
  link_with : [reporter_lib],
  include_directories : inc,
  dependencies : [threads_dep])
test('progress_reporter', reporter_test)

# ── Integration-test driver binaries ──────────────────────────────────────────
# These are not registered with test() — they're driven by the Docker harness
# in tests/integration/run_inside.sh, which builds the project and then
# executes them in an environment containing socat + a real progress_server.

executable('integration_reporter',
  sources : ['integration/integration_reporter.cpp'],
  link_with : [reporter_lib],
  include_directories : inc,
  dependencies : [threads_dep])

executable('integration_plotter',
  sources : ['integration/integration_plotter.cpp'],
  link_with : [hpgl_lib, sender_lib, tokenizer_lib, memcfg_lib],
  include_directories : inc,
  dependencies : [serial_dep, threads_dep])
