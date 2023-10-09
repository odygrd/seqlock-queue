#include "doctest/doctest.h"

#include "seqlock_queue/seqlock_queue.h"

TEST_SUITE_BEGIN("SeqlockQueue");

using namespace sq;

struct Test1
{
  uint64_t x;
  uint64_t y;
  uint32_t z;
};

/***/
TEST_CASE("produce_consume_full_queue_single_thread")
{
  constexpr size_t capacity{4};
  constexpr uint32_t iterations{2000};

  using seqlock_queue_t = sq::BoundedSeqlockQueue<Test1>;
  seqlock_queue_t seqlock_queue{capacity};

  sq::SeqlockQueueProducer<seqlock_queue_t> producer{seqlock_queue};
  sq::SeqlockQueueConsumer<seqlock_queue_t> consumer{seqlock_queue};

  Test1 result;
  REQUIRE_EQ(consumer.try_read(result), false);

  for (uint32_t iters = 0; iters < iterations; ++iters)
  {
    // write and read a full queue
    for (uint32_t i = 0; i < capacity; ++i)
    {
      Test1& value = producer.prepare_write();
      value.x = i + iters;
      value.y = i + iters + 100;
      value.z = i + iters + 200;
      producer.commit_write();
    }

    // read
    size_t total_reads{0};
    while (consumer.try_read(result))
    {
      REQUIRE_EQ(result.x, total_reads + iters);
      REQUIRE_EQ(result.y, total_reads + iters + 100);
      REQUIRE_EQ(result.z, total_reads + iters + 200);
      ++total_reads;
    }
    REQUIRE_EQ(total_reads, capacity);

    // queue is empty again
    REQUIRE_EQ(consumer.try_read(result), false);
  }

  // queue is empty again
  REQUIRE_EQ(consumer.try_read(result), false);
}

/***/
TEST_CASE("produce_consume_single_thread")
{
  constexpr size_t capacity{4};
  constexpr uint32_t iterations{20'000};

  using seqlock_queue_t = sq::BoundedSeqlockQueue<Test1>;
  seqlock_queue_t seqlock_queue{capacity};

  sq::SeqlockQueueProducer<seqlock_queue_t> producer{seqlock_queue};
  sq::SeqlockQueueConsumer<seqlock_queue_t> consumer{seqlock_queue};

  Test1 result;
  REQUIRE_EQ(consumer.try_read(result), false);

  for (uint32_t iters = 0; iters < iterations; ++iters)
  {
    Test1& value = producer.prepare_write();
    value.x = iters;
    value.y = iters * 100;
    value.z = iters + 200;
    producer.commit_write();

    // read
    REQUIRE_EQ(consumer.try_read(result), true);
    REQUIRE_EQ(result.x, iters);
    REQUIRE_EQ(result.y, iters * 100);
    REQUIRE_EQ(result.z, iters + 200);

    // queue is empty again
    REQUIRE_EQ(consumer.try_read(result), false);
  }

  // queue is empty again
  REQUIRE_EQ(consumer.try_read(result), false);
}

/***/
TEST_CASE("version_wrap_around")
{
  constexpr size_t capacity{4};
  constexpr uint32_t iterations{2000};

  using seqlock_queue_t = sq::BoundedSeqlockQueue<Test1>;
  seqlock_queue_t seqlock_queue{capacity};

  sq::SeqlockQueueProducer<seqlock_queue_t> producer{seqlock_queue};
  sq::SeqlockQueueConsumer<seqlock_queue_t> consumer{seqlock_queue};

  Test1 result;
  REQUIRE_EQ(consumer.try_read(result), false);

  // Producer produces but the consumer hasn't started consuming yet

  for (uint32_t iters = 0; iters < 128; ++iters)
  {
    // write a full queue
    for (uint32_t i = 0; i < capacity; ++i)
    {
      Test1& value = producer.prepare_write();
      value.x = i + iters;
      value.y = i + iters + 100;
      value.z = i + iters + 200;
      producer.commit_write();
    }
  }

  // version wrap around to 0
  for (uint32_t i = 0; i < 2; ++i)
  {
    Test1& value = producer.prepare_write();
    value.x = 1337;
    value.y = 1127;
    value.z = 11271;
    producer.commit_write();
  }

  // now we have version 0 0 254 254

  // Consumer stats reading and will only see 0 0 and not 254 254
  size_t total_reads{0};
  while (consumer.try_read(result))
  {
    REQUIRE_EQ(result.x, 1337);
    REQUIRE_EQ(result.y, 1127);
    REQUIRE_EQ(result.z, 11271);
    ++total_reads;
  }
  REQUIRE_EQ(total_reads, 2);

  // queue is empty again
  REQUIRE_EQ(consumer.try_read(result), false);
}

/***/
TEST_CASE("consume_then_version_wrap_around")
{
  constexpr size_t capacity{4};
  constexpr uint32_t iterations{2000};

  using seqlock_queue_t = sq::BoundedSeqlockQueue<Test1>;
  seqlock_queue_t seqlock_queue{capacity};

  sq::SeqlockQueueProducer<seqlock_queue_t> producer{seqlock_queue};
  sq::SeqlockQueueConsumer<seqlock_queue_t> consumer{seqlock_queue};

  Test1 result;
  REQUIRE_EQ(consumer.try_read(result), false);

  for (uint32_t iters = 0; iters < 2; ++iters)
  {
    // write and read a full queue.
    // we need to consume at least 2 queues to make _read_pos at least > 2, otherwise it
    // won't ready anything when version wraps around
    for (uint32_t i = 0; i < capacity; ++i)
    {
      Test1& value = producer.prepare_write();
      value.x = i;
      value.y = i;
      value.z = i;
      producer.commit_write();
    }

    // First consume a full queue, that will change the _read_pos consumer value
    size_t total_reads{0};
    while (consumer.try_read(result))
    {
      ++total_reads;
    }
    REQUIRE_EQ(total_reads, 4);
  }

  for (uint32_t iters = 0; iters < 126; ++iters)
  {
    for (uint32_t i = 0; i < capacity; ++i)
    {
      Test1& value = producer.prepare_write();
      value.x = i + iters;
      value.y = i + iters + 100;
      value.z = i + iters + 200;
      producer.commit_write();
    }
  }

  // version wrap around to 0
  for (uint32_t i = 0; i < 2; ++i)
  {
    Test1& value = producer.prepare_write();
    value.x = 1337;
    value.y = 1127;
    value.z = 11271;
    producer.commit_write();
  }

  // now we have version 0 0 254 254 after writing the above 2 Slots,
  // we expect to read only the first 2 items

  // read
  size_t total_reads{0};
  while (consumer.try_read(result))
  {
    REQUIRE_EQ(result.x, 1337);
    REQUIRE_EQ(result.y, 1127);
    REQUIRE_EQ(result.z, 11271);
    ++total_reads;
  }
  REQUIRE_EQ(total_reads, 2);

  // queue is empty again
  REQUIRE_EQ(consumer.try_read(result), false);
}

TEST_SUITE_END();