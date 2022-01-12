#ifndef OPTARG_HPP
#define OPTARG_HPP

/**
optarg

C++ lets you assign compile-time defaults to function arguments and such. This
header adds the ability to work with run-time defaults.

Consider a simple example like this:

	void foo(int i = 0) {
		std::cout << i << '\n';
	}

Calling foo(42) will print 42, but calling foo() with no arguments will still
print the default 0.

With OptArg, you could rewrite the function like this:

	struct foo_i{ using type = int };
	void foo(OptArg<foo_i> i = {}) {
		std::cout << i << '\n';
	}

For each argument with an optional value, you define a simple struct that
specifies the data type of that argument. Then you pass this struct as a
template argument into OptArg as shown above.

OptArg is built around C++17's std::optional class template. By default, you
want it to contain no value, so you can assign it either {} or std::nullopt in
the function prototype.

Now at this point, both versions of foo behave the same way, since the default-
constructed version of an int takes the value 0. But consider the following
calls:

	foo();
	WithDefArg<foo_i> def1{1};
	foo();

This should print:

	0
	1

WithDefArg is a companion struct to OptArg that lets you modify the default
value. It remains modified until the WithDefArg instance goes out of scope.

	foo();
	{
		WithDefArg<foo_i> def1{1};
		foo();
	}
	foo();

Output:
	0
	1
	0

You can even nest instances of WithDefArg.

	foo();
	{
		WithDefArg<foo_i> def1{1};
		foo();
		{
			WithDefArg<foo_i> def2{2};
			foo();
		}
		foo();
	}
	foo();

Output:
	0
	1
	2
	1
	0

Notice how the foo_i struct is passed to both OptArg and WithDefArg. Aside from
defining the data type, it serves as a unique identifier, or "tag class" in C++
parlance, of the default value. The value itself is stored as a thread_local
variable within OptArg, so there should be one default defined per thread per
tag.
**/

#include <optional>
#include <type_traits>
#include <utility>

namespace optarg {

	/**
	CustomDef struct template

	CustomDef gives you a bit more control over what the "root" default should
	be for a given data type. For example, let's say you want an int to default
	to -1 rather than 0 as it would if you wrote int{}. You could go with
	CustomDef<int,-1>{} to make this happen.

	In conjunction with OptArg, this would help you specify what goes into the
	thread_local default initially. So in the earlier example, you could write:

		struct foo_i { using type = CustomDef<int,-1>; };

	CustomDef is trivially constructed and has but a single data member called
	"value" that is initialized to the 2nd template argument. It does supply
	conversion operators and such to let you treat a CustomDef instance as
	though it were an instance of the data type you give in the 1st template
	argument.

	Note that C++ restricts what data types can appear as template arguments.
	Traditionally, you're looking at integral types for the most part, though
	under C++20, they expanded this to include floating-point and even some
	class types with restrictions.
	**/
	struct CustomDefBase {};
	template<typename T, T DefVal>
		struct CustomDef: CustomDefBase {
			using type = T;
			static constexpr type kDefVal = DefVal;
			type value = kDefVal;
			constexpr operator type&() noexcept { return value; }
			constexpr operator const type&() const noexcept { return value; }
			auto operator= (const CustomDef&) noexcept -> CustomDef& = default;
			constexpr auto operator= (const type& newVal) noexcept
				-> CustomDef& { return value = newVal, *this; }
		};

	/**
	OptArg struct template

	This data structure manages the value (if any) passed into the function,
	as well as its thread-local default.

	Template args:
		Tag:
			This is a data type that uniquely identifies a function argument for
			which you want to manage a default value. There will be one default
			per thread for every unique Tag type.
		Value:
			This is the data type you want your function to actually receive.
			By default, it is taken from the "type" type definition within the
			Tag struct, but you can supply it manually and not bother with that
			if you prefer. In other words, in the earlier example, you could
			have written:

				struct foo_i{};
				void foo(OptArg<foo_i,int> i = {}) //...

			Note that OptArg supplies a variety of conversion operators and what
			not to let you treat an OptArg as though it were a Value in most
			situations. There is some overhead to doing so however (see value()
			method for details).

	Implementation Note:
		Much of the functionality of OptArg is actually implemented in its
		parent struct: OptArgBase. The exception is the value() method and
		associated conversion operator. These have been specialized to handle
		CustomDef Value types seemlessly.
	**/
	template<typename OptArg, typename Tag, typename Value>
		struct OptArgBase {
			template<typename, typename> friend class WithDefArg;

			using TTag = Tag;
			using TValue = Value;
			using TOptVal = std::optional<TValue>;

			/**
			Make class method

			This is a utility function to help you build an OptArg's value
			in-place. OptArg<MyCoordsArg>::Make(1, 2) is equivalent to
			OptArg<MyCoordsArg>(std::in_place, 1, 2). It is essentially OptArg's
			counterpart to std::make_optional.
			**/
			template<typename... Args>
				static constexpr auto Make(Args&&... args) -> OptArg {
					return OptArg(std::in_place, std::forward<Args>(args)...);
				}
			template<typename T, typename... Args>
				static constexpr auto Make(
					std::initializer_list<T> ilist, Args&&... args) -> OptArg
				{
					return OptArg(
						std::in_place, ilist, std::forward<Args>(args)...
						);
				}

			/**
			GetDefault/SetDefault class methods

			These are low-level accessors that let you manage the default value
			yourself. Normally, you would use WithDefArg to do so instead.
			**/
			static auto GetDefault() noexcept -> const TValue&;
			static void SetDefault(TValue&& v) noexcept;
			static void SetDefault(const TValue& v);

			constexpr OptArgBase() noexcept = default;
			constexpr OptArgBase(const OptArgBase&) = default;
			constexpr OptArgBase(OptArgBase&&) noexcept = default;
			constexpr OptArgBase(std::nullopt_t nullOpt) noexcept:
				mOptVal{nullOpt} {}
			constexpr OptArgBase(const TOptVal& optVal): mOptVal{optVal} {}
			constexpr OptArgBase(TOptVal&& optVal) noexcept:
				mOptVal{std::move(optVal)} {}
			template<typename... Args>
				constexpr explicit OptArgBase(std::in_place_t ip, Args&&... args):
					mOptVal(ip, std::forward<Args>(args)...) {}
			template<typename T, typename... Args>
				constexpr explicit OptArgBase(
					std::in_place_t ip, std::initializer_list<T> ilist,
					Args&&... args
					):
					mOptVal(ip, ilist, std::forward<Args>(args)...) {}
			constexpr OptArgBase(TValue&& value):
				mOptVal{std::forward<TValue>(value)} {}

			constexpr auto operator= (OptArgBase&&) noexcept
				-> OptArgBase& = default;
			constexpr auto operator= (const OptArgBase&)
				-> OptArgBase& = default;

			/**
			defaults method:
				Returns: true if value() will give you the default value
			**/
			constexpr auto defaults() const -> bool {
				return !mOptVal.has_value();
			 }

			/**
			reset method:

			This clears the argument (if any) passed into the function so that
			value() will return the default from here on. (You can always
			re-assign a new value later on, however.)
			**/
			void reset() noexcept;

		 protected:
			static thread_local TValue tlDefVal;
			TOptVal mOptVal;
		};
	template<
		typename Tag,
		typename Value = typename Tag::type,
		typename Enable = void
		>
		struct OptArg: OptArgBase<OptArg<Tag,Value>,Tag,Value> {
			using OptArgBase<OptArg<Tag,Value>,Tag,Value>::OptArgBase;
			using TTag = Tag;
			using TValue = Value;

			/**
			value method:
				Though OptArg stores a std::optional<TValue> internally, this
				method should always return a TValue, since it can return the
				default if need be.

				Note that OptArg also has a TValue conversion operator, so you
				need not call value() explicitly.

				Both value() and the conversion operator will always perform the
				logic of looking up the default whenever necessary. In other
				words, the default is never cached internally. This has the
				advantage that if you were to change the default, value() may
				return its updated value, but it does add a small amount of
				overhead. You may want to cache it yourself in a local variable
				if you're going to be using it a lot.

				Returns: the value passed to the function or the default
			**/
			auto value() && -> TValue {
				return this->defaults() ?
					this->tlDefVal : std::move(*this->mOptVal);
			 }
			auto value() const& noexcept -> const TValue& {
				return this->defaults() ? this->tlDefVal : *this->mOptVal;
			 }
			operator TValue() && { return value(); }
			operator const TValue&() const& noexcept { return value(); }
		};
	template<typename Tag, typename Value>
		struct OptArg<
			Tag,
			Value,
			std::enable_if_t<std::is_base_of_v<CustomDefBase,Value>>
			>:
			OptArgBase<OptArg<Tag,Value>,Tag,Value>
		{
			using OptArgBase<OptArg<Tag,Value>,Tag,Value>::OptArgBase;
			using TTag = Tag;
			using TValue = typename Value::type;
			auto value() && -> TValue {
				return this->defaults() ?
					this->tlDefVal.value : std::move(this->mOptVal->value);
			}
			auto value() const& noexcept -> const TValue& {
				return this->defaults() ?
					this->tlDefVal.value : this->mOptVal->value;
			}
			operator TValue() && { return value(); }
			operator const TValue&() const& noexcept { return value(); }
		};
		

	/**
	WithDefArg struct template

	This struct is designed to be instanced as a local variable in a function
	where you want to change the default value.

	Template Args:
		Tag: see OptArg
		Value: see OptArg
	**/
	template<typename Tag, typename Value = typename Tag::type>
		struct WithDefArg {
			using TTag = Tag;
			using TValue = Value;

			/**
			Constructors

			This struct cannot be copy, move, or default-constructed. Rather,
			it is constructed from the value it is setting to be the new
			default. (The value itself can be copied or moved into place.)

			You can optionally include a second argument which is a callback
			functor. This functor would be responsible for merging the new
			default value into the old one. Normally, WithDefArg will simply
			replace the old value with the new, but say for example you have a
			default flags variable, and you would prefer new defaults be
			bit-wise or'd with the old. You could write:

				struct FlagsArg{ using type = int };
				auto orNewFlags = [](int& oldFlags, int newFlags) {
					oldFlags |= newFlags;
				};
				WithDefArg<FlagsArg> defFlags{0x1, orNewFlags};
			**/
			WithDefArg(const TValue& v);
			WithDefArg(TValue&& v) noexcept;
			template<typename MergeFn>
				WithDefArg(const TValue& v, MergeFn&& mergeFn);
			template<typename MergeFn>
				WithDefArg(TValue&& v, MergeFn&& mergeFn) noexcept;
			WithDefArg(const WithDefArg&) = delete;
			WithDefArg(WithDefArg&&) = delete;

			~WithDefArg() noexcept;

		private:
			TValue mSaved;
		};

	//==== Template Implementation =============================================

	//---- OptArgBase ----------------------------------------------------------

	template<typename C, typename T, typename V>
		auto OptArgBase<C,T,V>::GetDefault() noexcept -> const TValue& {
			return tlDefVal;
		}
	template<typename C, typename T, typename V>
		void OptArgBase<C,T,V>::SetDefault(TValue&& v) noexcept {
			tlDefVal = std::move(v);
		}
	template<typename C, typename T, typename V>
		void OptArgBase<C,T,V>::SetDefault(const TValue& v) {
			tlDefVal = v;
		}
	template<typename C, typename T, typename V>
		void OptArgBase<C,T,V>::reset() noexcept {
			return mOptVal.reset();
		}
	template<typename C, typename T, typename V>
		thread_local V OptArgBase<C,T,V>::tlDefVal{};

	//---- WithDefArg ----------------------------------------------------------

	template<typename T, typename V>
		WithDefArg<T,V>::WithDefArg(const TValue& v):
			mSaved{OptArg<T,V>::tlDefVal}
		{
			OptArg<T,V>::tlDefVal = v;
		}
	template<typename T, typename V>
		WithDefArg<T,V>::WithDefArg(TValue&& v) noexcept:
			mSaved{OptArg<T,V>::tlDefVal}
		{
			OptArg<T,V>::tlDefVal = std::move(v);
		}
	template<typename T, typename V> template<typename MergeFn>
		WithDefArg<T,V>::WithDefArg(const TValue& v, MergeFn&& mergeFn):
			mSaved{OptArg<T,V>::tlDefVal}
		{
			mergeFn(OptArg<T,V>::tlDefVal, v);
		}
	template<typename T, typename V> template<typename MergeFn>
		WithDefArg<T,V>::WithDefArg(TValue&& v, MergeFn&& mergeFn) noexcept:
			mSaved{OptArg<T,V>::tlDefVal}
		{
			mergeFn(OptArg<T,V>::tlDefVal, std::move(v));
		}
	template<typename T, typename V>
		WithDefArg<T,V>::~WithDefArg() noexcept {
			OptArg<T,V>::tlDefVal = std::move(mSaved);
		}
}

#endif
