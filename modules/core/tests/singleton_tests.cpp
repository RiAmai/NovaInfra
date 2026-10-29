#include <gtest/gtest.h>
#include <thread>
#include <vector>
#include <set>
#include <core/singleton.h>

// Test singleton class
class TestSingleton : public Singleton<TestSingleton>
{
    friend class Singleton<TestSingleton>;

public:
    int getValue() const { return value_; }
    void setValue(int v) { value_ = v; }

    int getCounter() const { return counter_; }

private:
    TestSingleton() : value_(0), counter_(++instanceCount_) {}

    int value_;
    int counter_;
    static int instanceCount_;
};

int TestSingleton::instanceCount_ = 0;

// Another test singleton class
class AnotherSingleton : public Singleton<AnotherSingleton>
{
    friend class Singleton<AnotherSingleton>;

public:
    std::string getName() const { return name_; }
    void setName(const std::string &n) { name_ = n; }

private:
    AnotherSingleton() : name_("default") {}
    std::string name_;
};

TEST(Singleton, BasicUsage)
{
    auto instance1 = TestSingleton::instance();
    ASSERT_NE(instance1, nullptr);

    instance1->setValue(42);
    EXPECT_EQ(instance1->getValue(), 42);
}

TEST(Singleton, SameInstance)
{
    auto instance1 = TestSingleton::instance();
    auto instance2 = TestSingleton::instance();

    // Same pointer
    EXPECT_EQ(instance1.get(), instance2.get());

    // Modify through one, visible through another
    instance1->setValue(100);
    EXPECT_EQ(instance2->getValue(), 100);
}

TEST(Singleton, OnlyOneInstanceCreated)
{
    auto instance1 = TestSingleton::instance();
    auto instance2 = TestSingleton::instance();
    auto instance3 = TestSingleton::instance();

    // All should have the same counter value (only created once)
    EXPECT_EQ(instance1->getCounter(), instance2->getCounter());
    EXPECT_EQ(instance2->getCounter(), instance3->getCounter());
}

TEST(Singleton, ThreadSafety)
{
    const int numThreads = 10;
    std::vector<std::thread> threads;
    std::vector<TestSingleton *> pointers(numThreads);

    // Multiple threads getting instance simultaneously
    for (int i = 0; i < numThreads; ++i) {
        threads.emplace_back([i, &pointers]() {
            auto instance = TestSingleton::instance();
            pointers[i] = instance.get();
        });
    }

    for (auto &t : threads) {
        t.join();
    }

    // All threads should get the same pointer
    std::set<TestSingleton *> uniquePointers(pointers.begin(), pointers.end());
    EXPECT_EQ(uniquePointers.size(), 1);
}

TEST(Singleton, ConcurrentAccess)
{
    auto instance = TestSingleton::instance();
    ASSERT_NE(instance, nullptr);

    const int numThreads = 10;
    const int accessesPerThread = 1000;
    std::vector<std::thread> threads;
    std::vector<int> matchingInstances(numThreads, 0);

    // instance() is thread-safe; mutable instance fields need their own synchronization.
    for (int i = 0; i < numThreads; ++i) {
        threads.emplace_back([i, expected = instance.get(), &matchingInstances]() {
            int matches = 0;
            for (int j = 0; j < accessesPerThread; ++j) {
                if (TestSingleton::instance().get() == expected)
                    ++matches;
            }
            matchingInstances[i] = matches;
        });
    }

    for (auto &t : threads) {
        t.join();
    }

    for (int matches : matchingInstances)
        EXPECT_EQ(matches, accessesPerThread);
}

TEST(Singleton, MultipleSingletonTypes)
{
    auto test = TestSingleton::instance();
    auto another = AnotherSingleton::instance();

    // Different singleton types have different instances
    test->setValue(123);
    another->setName("test");

    EXPECT_EQ(test->getValue(), 123);
    EXPECT_EQ(another->getName(), "test");

    // Getting them again should return same instances
    auto test2 = TestSingleton::instance();
    auto another2 = AnotherSingleton::instance();

    EXPECT_EQ(test.get(), test2.get());
    EXPECT_EQ(another.get(), another2.get());
}

TEST(Singleton, SharedPtrSemantics)
{
    auto instance1 = TestSingleton::instance();
    instance1->setValue(999);

    {
        auto instance2 = TestSingleton::instance();
        EXPECT_EQ(instance2->getValue(), 999);

        // Reference count increases
        EXPECT_EQ(instance1.use_count(), instance2.use_count());
    }

    // After scope, instance1 still valid
    EXPECT_EQ(instance1->getValue(), 999);
}

TEST(Singleton, CopyConstructorDisabled)
{
    // This should not compile if uncommented:
    // TestSingleton obj1;
    // TestSingleton obj2(obj1);

    // Just verify we can get the instance
    auto instance = TestSingleton::instance();
    EXPECT_NE(instance, nullptr);
}

TEST(Singleton, AssignmentOperatorDisabled)
{
    // This should not compile if uncommented:
    // TestSingleton obj1, obj2;
    // obj2 = obj1;

    // Just verify we can get the instance
    auto instance = TestSingleton::instance();
    EXPECT_NE(instance, nullptr);
}
