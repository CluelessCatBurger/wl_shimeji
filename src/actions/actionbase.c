#include "actionbase.h"
#include "expressions.h"


uint32_t mascot_duration_limit(struct mascot* mascot, struct mascot_expression* expr, uint32_t tick)
{
    float duration = 0.0;
    if (expr) {
        enum expression_execution_result res = expression_vm_execute(expr->body, mascot, &duration);
        if (res == EXPRESSION_EXECUTION_OK) {
            return tick + duration;
        }
        else {
            WARN("<Mascot:%s:%u> Duration calculation failed for expression id %u", mascot->prototype->name, mascot->id, expr->body->id);
        }
    }
    return 0;
}
