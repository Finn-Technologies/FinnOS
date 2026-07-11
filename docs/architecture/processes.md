# Processes and execution

> Status: Initial conceptual model
> Implementation: Not implemented

A process is an isolation and resource-ownership context; a thread is an execution context; a task is a schedulable unit; a system service is a privileged-purpose user-space component; an application instance is a user-visible application context; a background operation is work without a foreground scene; and a user session groups identity, services, and visible environments.

The intended lifecycle is conceptual: created, starting, running, suspended, stopping, stopped, or failed. Recovery and restart rules remain unresolved. `fork()` and Unix signals are not intended as native primitives; explicit process creation and typed event/message mechanisms are preferred.
