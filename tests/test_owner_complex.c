#include <ttak/mem/owner.h>
#include <ttak/mem/mem.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include "../tests/test_macros.h"

// Contexts for test
typedef struct {
    int secret_value;
    char name[32];
} user_ctx_t;

// Function to be executed in Child Owner
static void child_task(void *ctx, void *args) {
    user_ctx_t *u = (user_ctx_t *)ctx;
    int *input = (int *)args;
    
    printf("  [Child] Executing task. Secret: %d, Input: %d\n", u ? u->secret_value : -1, *input);
    
    if (u) {
        u->secret_value += *input;
    }
}

// Function to be executed in Root Owner
// It creates a Child Owner and runs a task inside it
static void root_supervisor(void *ctx, void *args) {
    user_ctx_t *root_ctx = (user_ctx_t *)ctx;
    printf("[Root] Supervisor running. Root Name: %s\n", root_ctx->name);
    
    // Create Child Owner with strict isolation
    ttak_owner_t *child = ttak_owner_create(TTAK_OWNER_STRICT_ISOLATION);
    ASSERT(child != NULL);
    
    // Register a resource for the child (distinct from root's resource)
    user_ctx_t child_res = { .secret_value = 100, .name = "ChildResource" };
    ttak_owner_register_resource(child, "child_res", &child_res);
    
    // Register function
    ttak_owner_register_func(child, "do_work", child_task);
    
    // Execute child task
    int input = 50;
    bool result = ttak_owner_execute(child, "do_work", "child_res", &input);
    ASSERT(result == true);
    ASSERT(child_res.secret_value == 150); // 100 + 50
    
    // Try to execute non-existent function
    result = ttak_owner_execute(child, "hack_kernel", NULL, NULL);
    ASSERT(result == false);
    
    // Verify Child cannot see Root's resource (Conceptually - in this mock, we just check logic)
    // Cleanup
    ttak_owner_destroy(child);
    printf("[Root] Child execution finished and destroyed.\n");
}

int main() {
    printf("=== Test: Complex Owner Hierarchy ===\n");
    
    // 1. Create Root Owner
    ttak_owner_t *root = ttak_owner_create(TTAK_OWNER_SAFE_DEFAULT);
    ASSERT(root != NULL);
    
    // 2. Setup Root Context
    user_ctx_t root_data = { .secret_value = 9999, .name = "RootAdmin" };
    ttak_owner_register_resource(root, "root_ctx", &root_data);
    ttak_owner_register_func(root, "supervisor_mode", root_supervisor);
    
    // 3. Execute Root Task
    printf("Starting Root Execution...\n");
    bool res = ttak_owner_execute(root, "supervisor_mode", "root_ctx", NULL);
    ASSERT(res == true);
    
    // 4. Cleanup
    ttak_owner_destroy(root);
    
    printf("=== Test: Owner Passed ===\n");
    return 0;
}
