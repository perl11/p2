#!perl
# p2-specific regression test for named-sub explicit 'return', bare
# early-exit 'return;', implicit last-expression return, and
# anonymous 'sub { ... }' closures parsing as a deferred value (not
# executed eagerly at definition time). Calling a coderef stored in a
# scalar ($cb->(), $cb(), $cb->call()) is a separate, still-open gap
# (see AGENTS.md) and deliberately not exercised here.

sub named_return {
    return 42;
}
say named_return();

sub implicit_return {
    99;
}
say implicit_return();

sub early_exit {
    say "before";
    return;
    say "after";
}
early_exit();
say "done";

say "before-anon-def";
my $cb = sub {
    say "inside-anon";
    return 7;
};
say "after-anon-def";
