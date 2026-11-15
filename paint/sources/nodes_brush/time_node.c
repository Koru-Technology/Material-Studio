
#include "../global.h"

typedef struct time_node {
	struct logic_node *base;
} time_node_t;

logic_node_value_t *time_node_get(time_node_t *self, i32 from) {
	if (from == 0) {
		logic_node_value_t *v = TMP_ALLOC_INIT(logic_node_value_t, {._f32 = sys_time()});
		return v;
	}
	else if (from == 1) {
		logic_node_value_t *v = TMP_ALLOC_INIT(logic_node_value_t, {._f32 = sys_delta()});
		return v;
	}
	else {
		logic_node_value_t *v = TMP_ALLOC_INIT(logic_node_value_t, {._f32 = g_context->brush_time});
		return v;
	}
}

void *time_node_create(ui_node_t *raw, f32_array_t *args) {
	time_node_t *n = ALLOC_INIT(time_node_t, {0});
	n->base        = logic_node_create(n);
	n->base->get   = time_node_get;
	return n;
}

void time_node_init() {
	any_map_set(nodes_brush_creates, "time_node", time_node_create);
}
