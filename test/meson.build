executable(
  'RunWalk',
  'RunWalk.cxx',
  '../src/Walk.cxx',
  '../src/system/SetupProcess.cxx',
  include_directories: inc,
  dependencies: [
    event_co_dep,
    event_uring_dep,
    time_dep,
  ],
)

executable(
  'RunCull',
  'RunCull.cxx',
  '../src/Cull.cxx',
  '../src/DevCachefiles.cxx',
  '../src/Walk.cxx',
  '../src/system/SetupProcess.cxx',
  include_directories: inc,
  dependencies: [
    event_co_dep,
    event_uring_dep,
    time_dep,
  ],
)
