-- Record of a single revision of the SDK for which one or more benchmarks have been run
--
create table revision (
  -- Name of this revision, e.g. '0.2.0' or 'jdoe/RUM-12345/some-change/1'
  name         text primary key,
  -- Time when the data for this revision was last updated, in milliseconds since Unix epoch UTC
  recorded_at  integer not null
);

-- Record of a single run of a particular benchmark
--
create table invocation (
  -- Row ID that uniquely identifies this benchmark invocation
  id              integer primary key,

  -- Name of the benchmark that was run, matching a filename from benchmarks/ sans extension
  benchmark_name  text not null,
  -- Name of the SDK revision that this benchmark was run against
  revision_name   text not null references revision(name) on delete cascade,
  -- Name of the platform on which the benchmark was run, e.g. 'darwin'
  platform        text not null,
  -- Name of the build configuration used to compile the SDK, e.g. 'release' for optimized or 'debug' for unoptimized
  build_config    text not null,
  -- Time when the benchmark was run, in milliseconds since Unix epoch UTC
  recorded_at     integer not null,

  -- Sample size for statistical aggregation of duration values (i.e. number of CPU-profiled benchmark runs)
  duration_num_samples integer not null,

  -- Time elapsed executing commands in the setup phase, in seconds
  setup_duration_median  real not null,
  setup_duration_mean    real not null,
  setup_duration_stddev  real not null,
  setup_duration_sem     real not null,
  setup_duration_iqr     real not null,
  setup_duration_ci95_lo real not null,
  setup_duration_ci95_hi real not null,
  -- Total bytes allocated - total bytes freed during setup phase (i.e. heap size at end)
  setup_net_bytes        integer not null,

  -- Time elapsed executing commands in the teardown phase, in seconds
  teardown_duration_median  real not null,
  teardown_duration_mean    real not null,
  teardown_duration_stddev  real not null,
  teardown_duration_sem     real not null,
  teardown_duration_iqr     real not null,
  teardown_duration_ci95_lo real not null,
  teardown_duration_ci95_hi real not null,
  -- Change in heap size during the teardown phase
  teardown_net_bytes        integer not null,

  unique(benchmark_name, revision_name, platform)
);
-- Index invocations by revision so querying the data for a specific revision is efficient
create index idx_invocation_revision on invocation(revision_name);
-- Index invocations by benchmark name so querying the set of distinct benchmarks is efficient
create index idx_invocation_benchmark_name on invocation(benchmark_name);

-- Record of a single execution of a repl command within a benchmark
--
create table command (
  -- Row ID that uniquely identifies this command execution
  id            integer primary key,
  -- ID of the benchmark invocation in which this command execution was profiled
  invocation_id integer not null references invocation(id) on delete cascade,

  -- Text that was printed by the repl to indicate the SDK operation executed, e.g. 'Core::Start()'
  label         text not null,

  -- Time taken to execute the command (i.e. how long the main thread was blocked), in seconds
  duration_median  real not null,
  duration_mean    real not null,
  duration_stddev  real not null,
  duration_sem     real not null,
  duration_iqr     real not null,
  duration_ci95_lo real not null,
  duration_ci95_hi real not null
);
-- Facilitate fast lookup of commands for a given benchmark invocation
create index idx_command_invocation_id on command(invocation_id);
-- Facilitate fast lookup of commands for specific SDK operations in a given invocation
create index idx_command_invocation_label on command(invocation_id, label);

-- Record of a single allocation event (malloc or free) that occurred while a command was running
--
create table alloc_event (
  -- Row ID that uniquely identifies this allocation event
  id           integer primary key,
  -- ID of the command execution during which this allocation event occurred
  command_id   integer not null references command(id) on delete cascade,
  -- If 1, this event represents a malloc/new call; if 0, it represents free/delete
  is_alloc     integer not null check (is_alloc in (0, 1)),
  -- Total number of bytes allocated or freed
  size         integer not null,
  -- Index of the thread this allocation occurred on: 0 is always the main thread, all others are background threads in the order in which they were noticed
  thread_index integer not null
);
-- Facilitate fast lookup of allocation events for a given command
create index idx_alloc_event_command_id on alloc_event(command_id);
