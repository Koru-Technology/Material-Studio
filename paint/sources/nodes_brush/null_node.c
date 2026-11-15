
#include "../global.h"

typedef struct null_node {
	struct logic_node *base;
} null_node_t;

// A zero value: the default of inputs of a type without a node of its own
logic_node_value_t *null_node_get(null_node_t *self, i32 from) {
	return TMP_ALLOC_INIT(logic_node_value_t, {0});
}

void *null_node_create(ui_node_t *raw, f32_array_t *args) {
	null_node_t *n = ALLOC_INIT(null_node_t, {0});
	n->base        = logic_node_create(n);
	n->base->get   = null_node_get;
	return n;
}

void null_node_init() {
	any_map_set(nodes_brush_creates, "null_node", null_node_create);
}
