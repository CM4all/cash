gtest = dependency('gtest', main: true,
                   include_type: 'system',
                   disabler: true,
                   required: get_option('test'),
                   fallback: ['gtest', 'gtest_main_dep'])

executable(
  'RunWalk',
  'RunWalk.cxx',
  '../src/Walk.cxx',
  '../src/system/SetupProcess.cxx',
  include_directories: inc,
  dependencies: [
    event_co_dep,
    event_dep,
    time_dep,
  ],
)

executable(
  'RunCull',
  'RunCull.cxx',
  '../src/Chdir.cxx',
  '../src/Cull.cxx',
  '../src/DevCachefiles.cxx',
  '../src/Walk.cxx',
  '../src/system/SetupProcess.cxx',
  include_directories: inc,
  dependencies: [
    event_co_dep,
    event_dep,
    time_dep,
  ],
)
