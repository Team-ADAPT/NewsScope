# Examples

## Basic Assessment
```cpp
newsscope::ScoringEngine engine;
engine.initialize("data/sources.csv", "data/suspicious_phrases.txt", "");

newsscope::Article article("id-1", "Headline", "Body text", "Reuters");
auto result = engine.assess_article(article);
```

## Batch Assessment
```cpp
std::vector<newsscope::Article> batch = {...};
auto results = engine.assess_batch(batch);
```

## Concurrent Processing
```cpp
newsscope::ThreadPool pool(8);
for (const auto& article : batch) {
  pool.enqueue([&engine, &article]() { (void)engine.assess_article(article); });
}
pool.wait_for_all();
```

## Run
- Build: `make all`
- Test: `make run-tests`
- Benchmarks:
  - `./build/benchmark_throughput`
  - `./build/benchmark_latency`
  - `./build/benchmark_memory`
