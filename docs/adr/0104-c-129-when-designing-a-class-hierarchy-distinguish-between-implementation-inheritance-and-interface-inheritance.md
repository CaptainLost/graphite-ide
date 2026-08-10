# 0104. C.129: When designing a class hierarchy, distinguish between implementation inheritance and interface inheritance

Status: proposed
Date: 2026-08-10
Source: C++ Core Guidelines, section C (Classes and class hierarchies), transcribed 2026-08-10

## Context

Implementation details in an interface make the interface brittle; that is, they
make its users vulnerable to having to recompile after changes in the
implementation. Data in a base class increases the complexity of implementing
the base and can lead to replication of code.

The two kinds are defined as follows. *Interface inheritance* is the use of
inheritance to separate users from implementations, in particular to allow
derived classes to be added and changed without affecting the users of base
classes. *Implementation inheritance* is the use of inheritance to simplify the
implementation of new facilities by making useful operations available to
implementers of related new operations — sometimes called "programming by
difference". A pure interface class is simply a set of pure virtual functions.

In early OOP, in the 1980s and 1990s, implementation inheritance and interface
inheritance were often mixed, and bad habits die hard; mixtures are still not
uncommon in old code bases and in old-style teaching material. The importance of
keeping the two kinds separate increases with the size of a hierarchy — dozens
of derived classes — with the length of time the hierarchy is used, perhaps
decades, and with the number of distinct organisations in which a hierarchy is
used, since it can be difficult to distribute an update to a base class.

```cpp
class Shape {   // BAD, mixed interface and implementation
public:
    Shape();
    Shape(Point ce = {0, 0}, Color co = none): cent{ce}, col {co} { /* ... */}

    Point center() const { return cent; }
    Color color() const { return col; }

    virtual void rotate(int) = 0;
    virtual void move(Point p) { cent = p; redraw(); }
    virtual void redraw();
    // ...
private:
    Point cent;
    Color col;
};

class Circle : public Shape {
public:
    Circle(Point c, int r) : Shape{c}, rad{r} { /* ... */ }
    // ...
private:
    int rad;
};

class Triangle : public Shape {
public:
    Triangle(Point p1, Point p2, Point p3); // calculate center
    // ...
};
```

The problems are these. As the hierarchy grows and more data is added to
`Shape`, the constructors get harder to write and maintain. Why calculate the
centre for the `Triangle` when it might never be used? And adding a data member
to `Shape` — a drawing style or a canvas — means all classes derived from
`Shape`, and all code using `Shape`, need to be reviewed, possibly changed, and
probably recompiled.

The implementation of `Shape::move()` is an example of implementation
inheritance: `move()` has been defined once and for all for every derived class.
The more code there is in such base class member function implementations, and
the more data is shared by placing it in the base, the more benefit is gained —
and the less stable the hierarchy is.

The same hierarchy rewritten using interface inheritance:

```cpp
class Shape {  // pure interface
public:
    virtual Point center() const = 0;
    virtual Color color() const = 0;

    virtual void rotate(int) = 0;
    virtual void move(Point p) = 0;

    virtual void redraw() = 0;
    // ...
};
```

Note that a pure interface rarely has constructors: there is nothing to
construct.

```cpp
class Circle : public Shape {
public:
    Circle(Point c, int r, Color c) : cent{c}, rad{r}, col{c} { /* ... */ }

    Point center() const override { return cent; }
    Color color() const override { return col; }
    // ...
private:
    Point cent;
    int rad;
    Color col;
};
```

The interface is now less brittle, but there is more work in implementing the
member functions; `center` has to be implemented by every class derived from
`Shape`.

The benefits of both can be had at once with dual hierarchies. There are many
ways of implementing the idea; the upstream illustration uses a
multiple-inheritance variant. First a hierarchy of interface classes:

```cpp
class Shape {   // pure interface
public:
    virtual Point center() const = 0;
    virtual Color color() const = 0;

    virtual void rotate(int) = 0;
    virtual void move(Point p) = 0;

    virtual void redraw() = 0;
    // ...
};

class Circle : public virtual Shape {   // pure interface
public:
    virtual int radius() = 0;
    // ...
};
```

Then the implementation classes, here named equivalently but in an `Impl`
namespace:

```cpp
class Impl::Shape : public virtual ::Shape { // implementation
public:
    // constructors, destructor
    // ...
    Point center() const override { /* ... */ }
    Color color() const override { /* ... */ }

    void rotate(int) override { /* ... */ }
    void move(Point p) override { /* ... */ }

    void redraw() override { /* ... */ }
    // ...
};

class Impl::Circle : public virtual ::Circle, public Impl::Shape {   // implementation
public:
    // constructors, destructor
    int radius() override { /* ... */ }
    // ...
};
```

Extending both hierarchies with a `Smiley` class follows the same pattern:

```cpp
class Smiley : public virtual Circle { // pure interface
public:
    // ...
};

class Impl::Smiley : public virtual ::Smiley, public Impl::Circle {   // implementation
public:
    // constructors, destructor
    // ...
};
```

There are now two hierarchies — interface `Smiley -> Circle -> Shape`, and
implementation `Impl::Smiley -> Impl::Circle -> Impl::Shape`. Since each
implementation derives from its interface as well as from its implementation
base class, the result is a lattice rather than a tree. `Shape` is a poor
example of a class with an implementation, but the technique is aimed at more
complex hierarchies.

The implementation hierarchy can be used directly, rather than through the
abstract interface:

```cpp
void work_with_shape(Shape&);

int user()
{
    Impl::Smiley my_smiley{ /* args */ };   // create concrete shape
    // ...
    my_smiley.some_member();        // use implementation class directly
    // ...
    work_with_shape(my_smiley);     // use implementation through abstract interface
    // ...
}
```

This is useful when the implementation class has members that are not offered in
the abstract interface, or when direct use of a member offers optimisation
opportunities — for instance if an implementation member function is `final`.

Two further notes. Pimpl is a related technique for separating interface and
implementation. And there is often a choice between offering common
functionality as implemented base class functions and as free-standing functions
in an implementation namespace; base classes give a shorter notation and easier
access to shared data in the base, at the cost of that functionality being
available only to users of the hierarchy.

## Decision

Decide, for each base class, whether it exists to be an interface or to supply
an implementation, and do not let one class be both.

- Make an interface base a pure abstract class: pure virtual functions, no data,
  no constructor, a virtual destructor.
- Put shared implementation in a separate base, in a free function, or in a
  member, and keep it out of any class users derive from as an interface.
- Do not add a data member to an interface base; treat any pressure to do so as
  a sign that the interface and the implementation have merged.
- Where both stability and reuse are needed, build a dual hierarchy: an
  interface hierarchy using virtual inheritance, and an implementation hierarchy
  whose classes derive from both their interface and their implementation base.
- Prefer a free function in an implementation namespace to an implemented base
  class member when the functionality is not specific to the hierarchy.

## Rationale

The cost of mixing the two is paid by people who are not looking at the base
class. Adding one member to `Shape` recompiles every user of `Shape`, changes
the layout of every derived class, and may force a change to every derived
constructor — none of which is visible from the edit that caused it.

The trade-off is stated honestly in the source and is worth repeating: the more
code and data live in the base, the more is gained in reuse and the less stable
the hierarchy becomes. The rule does not say to forgo implementation
inheritance; it says to know which one is being used, and to keep the stable
thing separate from the reusable thing.

## Consequences

### Positive

- An interface with no data can be extended and its implementations replaced
  without forcing a recompile.
- Derived classes hold only their own state, so their constructors do not grow
  as the hierarchy does.
- Naming the two kinds of inheritance gives a vocabulary for reviewing a
  hierarchy before it is built.

### Negative

- Pure interface inheritance duplicates work: `center()` has to be written by
  every derived class, which is exactly the replication implementation
  inheritance existed to avoid.
- Dual hierarchies double the number of types, require virtual inheritance, and
  bring its costs — a heavier object layout, indirect base access, and the rule
  that the most-derived class initialises the virtual base.
- The technique is unfamiliar enough that a hierarchy built this way carries a
  standing explanation cost.

### Neutral

- Pimpl, per [0038](0038-i-27-for-stable-library-abi-consider-the-pimpl-idiom.md),
  addresses the same brittleness by a different route.
- The choice between a base class member and a free function in an
  implementation namespace is left open, with the trade-off stated.

## Enforcement

- Flag a derived-to-base conversion to a base that has both data and virtual
  functions, except for calls from a derived class member to a base class
  member.

## Related

- [0099](0099-c-121-if-a-base-class-is-used-as-an-interface-make-it-a-pure-abstract-class.md)
  — the interface half of this rule.
- [0112](0112-c-137-use-virtual-bases-to-avoid-overly-general-base-classes.md) —
  the mechanism the dual hierarchy relies on.
- [0108](0108-c-133-avoid-protected-data.md) — the usual form data in a base
  takes.
- [0038](0038-i-27-for-stable-library-abi-consider-the-pimpl-idiom.md) — the
  related separation technique named in the source.
