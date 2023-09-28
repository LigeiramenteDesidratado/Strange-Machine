#define _CAT3(a, b, c) a##b##c
#define CAT3(a, b, c)  _CAT3(a, b, c)
#define FN(name)       CAT3(GEN_NAME, _, name)

struct FN(entry_map);

struct FN(entry_map)
{
	GEN_KEY_TYPE key;
	u32 hash;
	GEN_VALUE_TYPE value;
	struct FN(entry_map) * next;
};

struct FN(result)
{
	b8 ok;
	GEN_VALUE_TYPE value;
};

static struct FN(entry_map) * FN(entry_ctor)(struct arena *arena, GEN_KEY_TYPE key, u32 hash, GEN_VALUE_TYPE value)
{
	struct FN(entry_map) * result;

	result = arena_reserve(arena, sizeof(struct FN(entry_map)));
	// result = malloc(sizeof(struct FN(entry_map)));
	result->key = key;
	result->hash = hash;
	result->value = value;
	result->next = 0;

	return (result);
}

struct FN(map)
{
	struct FN(entry_map) * *entries;
	u32 count;
	u32 capcity;
};

struct FN(map) FN(map_make)(struct arena *arena);
struct FN(result) FN(map_put)(struct arena *arena, struct FN(map) * map, GEN_KEY_TYPE key, GEN_VALUE_TYPE value);
struct FN(result) FN(map_get)(struct FN(map) * map, GEN_KEY_TYPE key);
struct FN(result) FN(map_remove)(struct arena *arena, struct FN(map) * map, GEN_KEY_TYPE key);

void FN(for_each)(
    struct FN(map) * map, b8 (*cb)(GEN_KEY_TYPE key, GEN_VALUE_TYPE value, void *user_data), void *user_data);

static void
FN(sm__expand_if_necessary)(struct arena *arena, struct FN(map) * map)
{
	if (map->count > (map->capcity * 3 / 4))
	{
		u32 new_entries_capcity = map->capcity << 1;
		struct FN(entry_map) **new_entries =
		    arena_reserve(arena, new_entries_capcity * sizeof(struct FN(entry_map) *));
		memset(new_entries, 0x0, new_entries_capcity * sizeof(struct FN(entry_map) *));
		// struct FN(entry_map) **new_entries = calloc(new_entries_capcity, sizeof(struct FN(entry_map) *));

		for (u32 i = 0; i < map->capcity; ++i)
		{
			struct FN(entry_map) *entry = map->entries[i];
			while (entry != 0)
			{
				struct FN(entry_map) *next = entry->next;
				// return ((size_t)hash) & (bucket_count - 1);
				u32 index = entry->hash & (new_entries_capcity - 1);
				entry->next = new_entries[index];
				new_entries[index] = entry;
				entry = next;
			}
		}

		arena_free(arena, map->entries);
		map->entries = new_entries;
		map->capcity = new_entries_capcity;
	}
}

static u32
FN(sm__hash_key)(GEN_KEY_TYPE key)
{
	u32 result = 0;

	result = GEN_HASH_KEY_FN(key);

	result += ~(result << 9);
	result ^= (result >> 14);
	result += (result << 4);
	result ^= (result >> 10);

	return (result);
}

static b8
FN(sm__equal_keys)(GEN_KEY_TYPE key_a, u32 hash_a, GEN_KEY_TYPE key_b, u32 hash_b)
{
	b8 result = false;
	if (hash_a != hash_b) { return (result); }

	result = GEN_CMP_KEY_FN(key_a, key_b);

	return (result);
}

struct FN(map) FN(map_make)(struct arena *arena)
{
	struct FN(map) result;

	u32 min_bucket_count = 16 * 4 / 3;
	result.capcity = 1;
	while (result.capcity <= min_bucket_count)
	{
		result.capcity <<= 1; // entries count must be power of 2
	}

	result.entries = arena_reserve(arena, result.capcity * sizeof(struct FN(entry_map) *));
	memset(result.entries, 0x0, result.capcity * sizeof(struct FN(entry_map) *));
	// result.entries = calloc(result.capcity, sizeof(struct FN(entry_map) *));
	result.count = 0;

	return (result);
}

struct FN(result) FN(map_put)(struct arena *arena, struct FN(map) * map, GEN_KEY_TYPE key, GEN_VALUE_TYPE value)
{
	struct FN(result) result = {0};

	u32 hash = FN(sm__hash_key)(key);
	u32 index = (hash) & (map->capcity - 1);

	struct FN(entry_map) **entry = &map->entries[index];
	while (true)
	{
		struct FN(entry_map) *current = *entry;
		if (current == 0)
		{
			*entry = FN(entry_ctor)(arena, key, hash, value);
			map->count++;
			FN(sm__expand_if_necessary)(arena, map);
			return (result);
		}

		if (FN(sm__equal_keys)(current->key, current->hash, key, hash))
		{
			GEN_VALUE_TYPE old_value = current->value;
			current->value = value;

			result.ok = true;
			result.value = old_value;

			return (result);
		}

		entry = &current->next;
	}

	return (result);
}

struct FN(result) FN(map_get)(struct FN(map) * map, GEN_KEY_TYPE key)
{
	struct FN(result) result = {0};
	u32 hash = FN(sm__hash_key)(key);
	u32 index = (hash) & (map->capcity - 1);

	struct FN(entry_map) *entry = map->entries[index];
	while (entry != 0)
	{
		if (FN(sm__equal_keys)(entry->key, entry->hash, key, hash))
		{
			result.ok = true;
			result.value = entry->value;
			return (result);
		}

		entry = entry->next;
	}

	return (result);
}

struct FN(result) FN(map_remove)(struct arena *arena, struct FN(map) * map, GEN_KEY_TYPE key)
{
	struct FN(result) result = {0};
	u32 hash = FN(sm__hash_key)(key);
	u32 index = (hash) & (map->capcity - 1);

	struct FN(entry_map) **entry = &map->entries[index];
	struct FN(entry_map) * current;
	while ((current = *entry) != 0)
	{
		if (FN(sm__equal_keys)(current->key, current->hash, key, hash))
		{
			GEN_VALUE_TYPE value = current->value;
			*entry = current->next;
			arena_free(arena, current);
			// free(current);
			map->count--;

			result.ok = true;
			result.value = value;

			return (result);
		}

		entry = &current->next;
	}

	return (result);
}

void
FN(for_each)(struct FN(map) * map, b8 (*cb)(GEN_KEY_TYPE key, GEN_VALUE_TYPE value, void *user_data), void *user_data)
{
	for (u32 i = 0; i < map->capcity; ++i)
	{
		struct FN(entry_map) *entry = map->entries[i];

		while (entry != 0)
		{
			struct FN(entry_map) *next = entry->next;
			if (!cb(entry->key, entry->value, user_data)) { return; }
			entry = next;
		}
	}
}

#undef _CAT3
#undef CAT3
#undef FN
#undef GEN_NAME
#undef GEN_KEY_TYPE
#undef GEN_VALUE_TYPE
#undef GEN_HASH_KEY_FN
#undef GEN_CMP_KEY_FN
