# Tutorial 20 – Owner Subsystem

[`modules/20-owner-subsystem`](../modules/20-owner-subsystem/README.md) walks through the sandbox owner tree in `src/mem/owner.c`.

`lesson20_owner_subsystem.c` registers a resource + function so you can trace how callbacks execute under an owner context.

## Checklist

1. Diagram owner creation, resource registration, and function execution, paying attention to isolation modes and inheritance.
2. Rebuild `ttak_owner_create/destroy/register_*` and `ttak_owner_execute`, making sure the dispatch table matches the lesson.
3. Run `make tests/test_owner_complex` plus any scenario you encode in this folder (nested owners, denied access).
4. Record policy decisions (naming convention, error strings) because the unsafe lessons rely on the same contracts.
