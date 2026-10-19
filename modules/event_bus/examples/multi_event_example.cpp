#include <event_bus/event_bus.h>

#include <chrono>
#include <iostream>
#include <memory>
#include <string>
#include <thread>

// ============================================================================
// Event Types - Different event structures for different purposes
// ============================================================================

struct UserLoginEvent {
    std::string username;
    std::string ipAddress;
    std::chrono::system_clock::time_point timestamp;
};

struct UserLogoutEvent {
    std::string username;
    int sessionDurationSeconds;
};

struct OrderCreatedEvent {
    int orderId;
    std::string customerId;
    double amount;
};

struct OrderShippedEvent {
    int orderId;
    std::string trackingNumber;
};

struct SystemHealthEvent {
    int cpuUsage;
    int memoryUsage;
    bool healthy;
};

struct NotificationEvent {
    std::string userId;
    std::string message;
    enum class Priority { LOW, NORMAL, HIGH, URGENT } priority;
};

// ============================================================================
// Event Handlers
// ============================================================================

class UserActivityLogger
{
public:
    void onLogin(const UserLoginEvent &event)
    {
        std::cout << "[UserActivityLogger] User '" << event.username << "' logged in from " << event.ipAddress
                  << "\n";
    }

    void onLogout(const UserLogoutEvent &event)
    {
        std::cout << "[UserActivityLogger] User '" << event.username << "' logged out after "
                  << event.sessionDurationSeconds << " seconds\n";
    }
};

class OrderProcessor
{
public:
    void onOrderCreated(const OrderCreatedEvent &event)
    {
        std::cout << "[OrderProcessor] Processing new order #" << event.orderId << " from customer "
                  << event.customerId << " ($" << event.amount << ")\n";
    }

    void onOrderShipped(const OrderShippedEvent &event)
    {
        std::cout << "[OrderProcessor] Order #" << event.orderId
                  << " shipped. Tracking: " << event.trackingNumber << "\n";
    }
};

class SystemMonitor
{
public:
    void onHealthUpdate(const SystemHealthEvent &event)
    {
        std::cout << "[SystemMonitor] Health check: CPU=" << event.cpuUsage
                  << "%, Memory=" << event.memoryUsage
                  << "%, Status=" << (event.healthy ? "HEALTHY" : "UNHEALTHY") << "\n";
    }
};

class NotificationService
{
public:
    void onNotification(const NotificationEvent &event)
    {
        const char *priorityStr[] = {"LOW", "NORMAL", "HIGH", "URGENT"};
        std::cout << "[NotificationService] Sending " << priorityStr[static_cast<int>(event.priority)]
                  << " priority notification to user " << event.userId << ": " << event.message << "\n";
    }
};

// ============================================================================
// Main Example
// ============================================================================

int main()
{
    std::cout << "=== Multi-Event Type EventBus Example ===\n\n";

    // Configure EventBus with a thread pool executor
    novainfra::eventbus::EventBusOptions options;
    options.executor = [](novainfra::eventbus::EventTask task) {
        // Synchronous execution for this example
        task();
        return true;
    };

    novainfra::eventbus::EventBus bus(options);

    // Create handler instances
    auto userLogger = std::make_shared<UserActivityLogger>();
    auto orderProcessor = std::make_shared<OrderProcessor>();
    auto systemMonitor = std::make_shared<SystemMonitor>();
    auto notificationService = std::make_shared<NotificationService>();

    // Subscribe to events - each event type has its own subscription
    std::cout << "1. Subscribing to multiple event types...\n\n";

    auto loginSub =
        bus.subscribe<UserLoginEvent>([userLogger](const UserLoginEvent &e) { userLogger->onLogin(e); });

    auto logoutSub =
        bus.subscribe<UserLogoutEvent>([userLogger](const UserLogoutEvent &e) { userLogger->onLogout(e); });

    auto orderCreatedSub = bus.subscribe<OrderCreatedEvent>(
        [orderProcessor](const OrderCreatedEvent &e) { orderProcessor->onOrderCreated(e); });

    auto orderShippedSub = bus.subscribe<OrderShippedEvent>(
        [orderProcessor](const OrderShippedEvent &e) { orderProcessor->onOrderShipped(e); });

    auto healthSub = bus.subscribe<SystemHealthEvent>(
        [systemMonitor](const SystemHealthEvent &e) { systemMonitor->onHealthUpdate(e); });

    auto notificationSub = bus.subscribe<NotificationEvent>(
        [notificationService](const NotificationEvent &e) { notificationService->onNotification(e); });

    // Multiple subscribers to the same event type
    auto notificationAuditor = bus.subscribe<NotificationEvent>([](const NotificationEvent &e) {
        std::cout << "[Auditor] Logged notification to user " << e.userId << "\n";
    });

    std::cout << "2. Publishing various events...\n\n";

    // Publish user events
    bus.publish(UserLoginEvent{"alice", "192.168.1.100", std::chrono::system_clock::now()});
    bus.publish(UserLoginEvent{"bob", "192.168.1.101", std::chrono::system_clock::now()});

    // Publish order events
    bus.publish(OrderCreatedEvent{1001, "CUST-001", 299.99});
    bus.publish(OrderCreatedEvent{1002, "CUST-002", 149.50});
    bus.publish(OrderShippedEvent{1001, "TRACK-ABC123"});

    // Publish system health events
    bus.publish(SystemHealthEvent{45, 60, true});
    bus.publish(SystemHealthEvent{85, 92, false});

    // Publish notifications (demonstrating multiple subscribers)
    bus.publish(NotificationEvent{"alice", "Your order has shipped", NotificationEvent::Priority::NORMAL});
    bus.publish(
        NotificationEvent{"bob", "System maintenance in 10 minutes", NotificationEvent::Priority::URGENT});

    // User logout
    bus.publish(UserLogoutEvent{"alice", 1847});

    std::cout << "\n3. Demonstrating async publish...\n\n";

    // Publish async events
    bus.publishAsync(NotificationEvent{"alice", "Async notification", NotificationEvent::Priority::LOW});
    bus.publishAsync(SystemHealthEvent{30, 55, true});

    std::cout << "\n4. Unsubscribing specific handlers...\n\n";

    // Unsubscribe notification auditor
    notificationAuditor.cancel();
    std::cout << "   Auditor unsubscribed. Publishing notification again:\n";
    bus.publish(NotificationEvent{"bob", "After auditor unsubscribed", NotificationEvent::Priority::NORMAL});

    std::cout << "\n5. Shutting down EventBus...\n";
    bus.shutdown();

    std::cout << "\n=== Example completed successfully ===\n";
    return 0;
}
