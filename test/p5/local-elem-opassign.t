#!perl
# local $h{k} OP= EXPR: the element is localized (undef), then the assignment runs; the old value comes back at the block end
our %h = (k => "old", n => 5);
our @a = (10, 20);
{
    local $h{k} .= "new";
    local $h{n} += 2;
    local $a[0] ||= 3;
    print "$h{k}|$h{n}|$a[0]\n";             #=> new|2|3
}
print "$h{k}|$h{n}|$a[0]\n";                 #=> old|5|10
