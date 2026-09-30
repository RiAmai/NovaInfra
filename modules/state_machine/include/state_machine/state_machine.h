#pragma once

#include <core/export.h>
#include <core/macros.h>
#include <core/namespace.h>

#if defined(NOVAINFRA_NO_EXCEPTIONS)
#  undef BOOST_SML_DISABLE_EXCEPTIONS
#  define BOOST_SML_DISABLE_EXCEPTIONS 1
#endif

#include <boost/sml.hpp>

NOVAINFRA_NAMESPACE_BEGIN
namespace statemachine {

/** @brief Boost.SML 的底层命名空间别名，便于高级用户访问原生能力。 */
namespace sml = ::boost::sml;

/** @brief 定义一个带类型数据的事件。 */
using ::boost::sml::event;
/** @brief 创建编译期状态转移表。 */
using ::boost::sml::make_transition_table;
/** @brief 状态进入事件标记。 */
using ::boost::sml::on_entry;
/** @brief 状态退出事件标记。 */
using ::boost::sml::on_exit;
/** @brief 定义一个状态。 */
using ::boost::sml::state;
/** @brief 未处理事件的内部事件类型。 */
using ::boost::sml::unexpected_event;
/** @brief 表示状态机终止状态。 */
using ::boost::sml::X;

/** @brief 延迟当前事件，稍后在状态转移完成后重新处理。 */
using ::boost::sml::defer;
/** @brief 为延迟事件启用队列策略。 */
using ::boost::sml::defer_queue;
/** @brief 为转移动作提供日志策略。 */
using ::boost::sml::logger;
/** @brief 将事件加入状态机内部处理队列。 */
using ::boost::sml::process;
/** @brief 为内部处理事件启用队列策略。 */
using ::boost::sml::process_queue;
/** @brief 为状态机事件处理启用互斥保护。 */
using ::boost::sml::thread_safe;

/**
 * @brief NovaInfra 对 Boost.SML 状态机的统一类型别名。
 *
 * Definition 的 operator() 返回转移表；Policies 可用于启用线程安全、
 * 延迟事件、内部事件队列和日志等策略。
 */
template <typename Definition, typename... Policies>
using StateMachine = ::boost::sml::sm<Definition, Policies...>;

namespace testing {
/** @brief 提供可设置当前状态能力的测试状态机类型别名。 */
template <typename Definition, typename... Policies>
using StateMachine = ::boost::sml::sm<Definition, ::boost::sml::testing, Policies...>;
}  // 测试命名空间

}  // 状态机命名空间
NOVAINFRA_NAMESPACE_END
