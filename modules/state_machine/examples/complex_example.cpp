#include <state_machine/runtime.h>
#include <state_machine/state_machine.h>

#include <deque>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

// ============================================================================
// Example 1: Order Processing State Machine
// ============================================================================

// States
struct OrderPending {
};
struct OrderValidating {
};
struct OrderProcessing {
};
struct OrderShipped {
};
struct OrderDelivered {
};
struct OrderCancelled {
};
struct OrderRefunding {
};
struct OrderRefunded {
};

// Events
struct ValidateOrder {
};
struct ValidationSuccess {
};
struct ValidationFailed {
    std::string reason;
};
struct ProcessPayment {
};
struct PaymentSuccess {
};
struct PaymentFailed {
};
struct ShipOrder {
};
struct DeliverOrder {
};
struct CancelOrder {
    std::string reason;
};
struct RequestRefund {
};
struct RefundProcessed {
};

struct OrderStateMachine {
    auto operator()() const
    {
        using namespace novainfra::statemachine;

        // Guard: only allow cancellation before shipping
        const auto canCancel = [](const CancelOrder &) { return true; };

        return make_transition_table(
            // Initial flow: Pending -> Validating -> Processing -> Shipped -> Delivered
            *state<OrderPending> + event<ValidateOrder> = state<OrderValidating>,
            state<OrderValidating> + event<ValidationSuccess> = state<OrderProcessing>,
            state<OrderValidating> + event<ValidationFailed> = state<OrderCancelled>,
            state<OrderProcessing> + event<PaymentSuccess> = state<OrderShipped>,
            state<OrderProcessing> + event<PaymentFailed> = state<OrderCancelled>,
            state<OrderShipped> + event<DeliverOrder> = state<OrderDelivered>,

            // Cancellation paths
            state<OrderPending> + event<CancelOrder>[canCancel] = state<OrderCancelled>,
            state<OrderValidating> + event<CancelOrder>[canCancel] = state<OrderCancelled>,
            state<OrderProcessing> + event<CancelOrder>[canCancel] = state<OrderCancelled>,

            // Refund flow
            state<OrderDelivered> + event<RequestRefund> = state<OrderRefunding>,
            state<OrderCancelled> + event<RequestRefund> = state<OrderRefunding>,
            state<OrderRefunding> + event<RefundProcessed> = state<OrderRefunded>);
    }
};

void demonstrateOrderProcessing()
{
    std::cout << "\n=== Example 1: Order Processing State Machine ===\n";

    using Machine = novainfra::statemachine::StateMachine<OrderStateMachine>;
    Machine machine;

    std::deque<novainfra::statemachine::EventTask> queue;
    novainfra::statemachine::RuntimeOptions options;
    options.executor = [&queue](novainfra::statemachine::EventTask task) {
        queue.push_back(std::move(task));
        return true;
    };
    novainfra::statemachine::StateMachineRuntime runtime(machine, options);

    auto checkState = [&](const std::string &expected) {
        while (!queue.empty()) {
            auto task = std::move(queue.front());
            queue.pop_front();
            task();
        }
        std::cout << "  Current state: " << expected << "\n";
    };

    std::cout << "Scenario A: Successful order\n";
    runtime.post(ValidateOrder{});
    checkState("OrderValidating");

    runtime.post(ValidationSuccess{});
    checkState("OrderProcessing");

    runtime.post(PaymentSuccess{});
    checkState("OrderShipped");

    runtime.post(DeliverOrder{});
    checkState("OrderDelivered");

    std::cout << "\nScenario B: Order with refund\n";
    Machine machine2;
    novainfra::statemachine::StateMachineRuntime runtime2(machine2, options);

    runtime2.post(ValidateOrder{});
    runtime2.post(ValidationSuccess{});
    runtime2.post(PaymentSuccess{});
    runtime2.post(DeliverOrder{});
    checkState("OrderDelivered");

    runtime2.post(RequestRefund{});
    checkState("OrderRefunding");

    runtime2.post(RefundProcessed{});
    checkState("OrderRefunded");

    std::cout << "\nScenario C: Cancelled order\n";
    Machine machine3;
    novainfra::statemachine::StateMachineRuntime runtime3(machine3, options);

    runtime3.post(ValidateOrder{});
    checkState("OrderValidating");

    runtime3.post(CancelOrder{"Customer requested"});
    checkState("OrderCancelled");
}

// ============================================================================
// Example 2: Access Control State Machine
// ============================================================================

// States
struct DoorLocked {
};
struct DoorAuthenticating {
};
struct DoorUnlocked {
};
struct DoorOpen {
};
struct DoorAlarmActive {
};

// Events
struct SwipeCard {
    std::string cardId;
};
struct AuthSuccess {
};
struct AuthFailed {
};
struct UnlockTimeout {
};
struct DoorOpened {
};
struct DoorClosed {
};
struct AlarmTriggered {
};
struct AlarmReset {
};

struct AccessControlMachine {
    auto operator()() const
    {
        using namespace novainfra::statemachine;

        const auto validCard = [](const SwipeCard &event) {
            return event.cardId == "VALID_CARD_001" || event.cardId == "VALID_CARD_002";
        };

        return make_transition_table(
            // Normal flow
            *state<DoorLocked> + event<SwipeCard>[validCard] = state<DoorAuthenticating>,
            state<DoorAuthenticating> + event<AuthSuccess> = state<DoorUnlocked>,
            state<DoorAuthenticating> + event<AuthFailed> = state<DoorLocked>,
            state<DoorUnlocked> + event<DoorOpened> = state<DoorOpen>,
            state<DoorUnlocked> + event<UnlockTimeout> = state<DoorLocked>,
            state<DoorOpen> + event<DoorClosed> = state<DoorLocked>,

            // Alarm scenarios
            state<DoorLocked> + event<AlarmTriggered> = state<DoorAlarmActive>,
            state<DoorAlarmActive> + event<AlarmReset> = state<DoorLocked>);
    }
};

void demonstrateAccessControl()
{
    std::cout << "\n=== Example 2: Access Control State Machine ===\n";

    using Machine = novainfra::statemachine::StateMachine<AccessControlMachine>;
    Machine machine;

    std::deque<novainfra::statemachine::EventTask> queue;
    novainfra::statemachine::RuntimeOptions options;
    options.executor = [&queue](novainfra::statemachine::EventTask task) {
        queue.push_back(std::move(task));
        return true;
    };
    novainfra::statemachine::StateMachineRuntime runtime(machine, options);

    auto processEvents = [&queue]() {
        while (!queue.empty()) {
            auto task = std::move(queue.front());
            queue.pop_front();
            task();
        }
    };

    std::cout << "Scenario A: Valid card access\n";
    runtime.post(SwipeCard{"VALID_CARD_001"});
    processEvents();
    std::cout << "  Door authenticating...\n";

    runtime.post(AuthSuccess{});
    processEvents();
    std::cout << "  Door unlocked\n";

    runtime.post(DoorOpened{});
    processEvents();
    std::cout << "  Door opened\n";

    runtime.post(DoorClosed{});
    processEvents();
    std::cout << "  Door closed and locked\n";

    std::cout << "\nScenario B: Invalid card\n";
    runtime.post(SwipeCard{"INVALID_CARD"});
    processEvents();
    std::cout << "  Card rejected, door remains locked\n";

    std::cout << "\nScenario C: Alarm triggered\n";
    runtime.post(AlarmTriggered{});
    processEvents();
    std::cout << "  Alarm active!\n";

    runtime.post(AlarmReset{});
    processEvents();
    std::cout << "  Alarm reset, door locked\n";
}

// ============================================================================
// Example 3: Game Character State Machine
// ============================================================================

// States
struct CharacterIdle {
};
struct CharacterWalking {
};
struct CharacterRunning {
};
struct CharacterJumping {
};
struct CharacterAttacking {
};
struct CharacterDead {
};

// Events
struct StartWalk {
};
struct StartRun {
};
struct Jump {
};
struct Attack {
};
struct Stop {
};
struct TakeDamage {
    int damage;
};
struct Respawn {
};

struct GameCharacterMachine {
    int health = 100;

    auto operator()() const
    {
        using namespace novainfra::statemachine;

        return make_transition_table(
            // Movement transitions
            *state<CharacterIdle> + event<StartWalk> = state<CharacterWalking>,
            state<CharacterIdle> + event<StartRun> = state<CharacterRunning>,
            state<CharacterIdle> + event<Jump> = state<CharacterJumping>,
            state<CharacterIdle> + event<Attack> = state<CharacterAttacking>,

            state<CharacterWalking> + event<Stop> = state<CharacterIdle>,
            state<CharacterWalking> + event<StartRun> = state<CharacterRunning>,
            state<CharacterWalking> + event<Jump> = state<CharacterJumping>,

            state<CharacterRunning> + event<Stop> = state<CharacterIdle>,
            state<CharacterRunning> + event<StartWalk> = state<CharacterWalking>,
            state<CharacterRunning> + event<Jump> = state<CharacterJumping>,

            state<CharacterJumping> + event<Stop> = state<CharacterIdle>,
            state<CharacterAttacking> + event<Stop> = state<CharacterIdle>,

            // Death and respawn
            state<CharacterIdle> + event<TakeDamage> = state<CharacterDead>,
            state<CharacterWalking> + event<TakeDamage> = state<CharacterDead>,
            state<CharacterRunning> + event<TakeDamage> = state<CharacterDead>,
            state<CharacterJumping> + event<TakeDamage> = state<CharacterDead>,
            state<CharacterAttacking> + event<TakeDamage> = state<CharacterDead>,

            state<CharacterDead> + event<Respawn> = state<CharacterIdle>);
    }
};

void demonstrateGameCharacter()
{
    std::cout << "\n=== Example 3: Game Character State Machine ===\n";

    using Machine = novainfra::statemachine::StateMachine<GameCharacterMachine>;
    Machine machine;

    std::deque<novainfra::statemachine::EventTask> queue;
    novainfra::statemachine::RuntimeOptions options;
    options.executor = [&queue](novainfra::statemachine::EventTask task) {
        queue.push_back(std::move(task));
        return true;
    };
    novainfra::statemachine::StateMachineRuntime runtime(machine, options);

    auto processAndLog = [&](const std::string &action) {
        while (!queue.empty()) {
            auto task = std::move(queue.front());
            queue.pop_front();
            task();
        }
        std::cout << "  " << action << "\n";
    };

    std::cout << "Character action sequence:\n";

    runtime.post(StartWalk{});
    processAndLog("Started walking");

    runtime.post(StartRun{});
    processAndLog("Started running");

    runtime.post(Jump{});
    processAndLog("Jumped");

    runtime.post(Stop{});
    processAndLog("Stopped (back to idle)");

    runtime.post(Attack{});
    processAndLog("Attacking");

    runtime.post(Stop{});
    processAndLog("Attack finished");

    std::cout << "\nCombat scenario:\n";
    runtime.post(StartRun{});
    processAndLog("Running");

    runtime.post(TakeDamage{100});
    processAndLog("Took fatal damage - Character dead!");

    runtime.post(Respawn{});
    processAndLog("Respawned at checkpoint");
}

// ============================================================================
// Main
// ============================================================================

int main()
{
    std::cout << "=== Complex State Machine Examples ===\n";

    demonstrateOrderProcessing();
    demonstrateAccessControl();
    demonstrateGameCharacter();

    std::cout << "\n=== All examples completed ===\n";
    return 0;
}
