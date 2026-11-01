#!perl
# chained list assignment: local (@a) = local (@b) = LIST, my/our variants
@bee = ('foo', 'bar', 'burbl', 'blah');
{
    local @bee = @bee;
    {
        local (@bim) = local(@bee) = qw(foo bar);
        print "A|@bee|@bim\n";              #=> A|foo bar|foo bar
    }
    print "B|@bee\n";                       #=> B|foo bar burbl blah
}
print "C|@bee\n";                           #=> C|foo bar burbl blah
our @a = @b = (3, 4);
print scalar(@a), "|", $a[1], "\n";         #=> 2|4
my (@m) = my (@n) = qw(x y);
print "D|@m|@n\n";                          #=> D|x y|x y
