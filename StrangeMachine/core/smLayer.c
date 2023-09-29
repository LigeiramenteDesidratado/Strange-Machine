#include "core/smArray.h"
#include "core/smCore.h"
#include "core/smString.h"

struct layer
layer_make(str8 name, void *user_data, layer_f on_attach, layer_f on_update, layer_f on_draw, layer_f on_detach)
{
	struct layer result;

	result.name = name;

	result.user_data = user_data;

	result.on_attach = on_attach;
	result.on_update = on_update;
	result.on_draw = on_draw;
	result.on_detach = on_detach;

	return (result);
}

void
layer_release(struct layer *layer)
{
	sm__assert(layer);
}

struct stack_layer
stack_layer_make(void)
{
	struct stack_layer result = {0};

	return (result);
}

void
stack_layer_release(struct stack_layer *stack_layer)
{
	stack_layer->layer_len = 0;
	stack_layer->overlayer_len = 0;
	// if (stack_layer->layers) { array_release(stack_layer->layers); }
	// if (stack_layer->overlayers) { array_release(stack_layer->overlayers); }
}

void
stack_layer_push(struct stack_layer *stack_layer, struct layer layer)
{
	sm__assert(stack_layer->layer_len <= 8);
	stack_layer->layers[stack_layer->layer_len++] = layer;
	// array_push(stack_layer->layers, layer);
}

void
stack_layer_push_overlayer(struct stack_layer *stack_layer, struct layer overlayer)
{
	sm__assert(stack_layer->layer_len <= 8);
	stack_layer->layers[stack_layer->overlayer_len++] = overlayer;
}

void
stack_layer_pop(struct stack_layer *stack_layer)
{
	stack_layer->layer_len--;
}

void
stack_layer_pop_overlayer(struct stack_layer *stack_layer)
{
	stack_layer->overlayer_len--;
}

u32
stack_layer_get_len(struct stack_layer *stack_layer)
{
	return (stack_layer->overlayer_len + stack_layer->layer_len);
}

struct layer *
stack_layer_get_layer(struct stack_layer *stack_layer, u32 index)
{
	sm__assert(index < 16);

	if (index < stack_layer->layer_len) { return (&stack_layer->layers[index]); }

	return (&stack_layer->overlayers[index - stack_layer->layer_len]);
}
