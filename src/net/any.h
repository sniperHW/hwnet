#ifndef _ANY_H
#define _ANY_H

#ifdef _STD_ANY

#include <any>

namespace hwnet {

typedef std::any any;
#define any_cast std::any_cast

}

#else

namespace hwnet {

class any
{

public: // structors

   any(): content(nullptr),counter(nullptr){}

   template<typename ValueType>
   any(const ValueType &value):counter(new int(1))
   {
	   content = create_holder<ValueType>(value);
   }

   any(const any & other)
   {
	   if(other.counter && other.content)
	   {
		   content = other.content;
		   counter = other.counter;
		   ++(*counter);
	   }
	   else
	   {
		   content = nullptr;
		   counter = nullptr;
	   }
   }

   any(any && other):content(other.content),counter(other.counter)
   {	
   		other.content = nullptr;
   		other.counter = nullptr;
   }

   	any & operator=(const any & rhs)
	{
		if(&rhs == this)
			return *this;
		else
		{      
			_destroy();
			if(rhs.counter && rhs.content)
			{
				content = rhs.content;
				counter = rhs.counter;
				++(*counter);
			}
			else
			{
				content = nullptr;
			}
			return *this;
		}
	}
	
   ~any()
   {
		_destroy();
   }

    bool empty() const
    {
       return !content;
    }

     class placeholder
     {
       public: // structors
	   	 virtual ~placeholder(){}
     };

     template<typename ValueType>
     class holder : public placeholder
     {
       public: // structors
         explicit holder(const ValueType &value): held(value){
         }
		 ValueType held;
      };
	  	  
	  template<typename ValueType>
	  placeholder * create_holder(ValueType v)
	  {
		 return new holder<ValueType>(v);
	  }

	  template<typename ValueType>
	  static ValueType cast(const any & operand)
	  {
		return static_cast<any::holder<ValueType> *>(operand.content)->held;
	  }	

	  template<typename ValueType>
	  static ValueType cast(any & operand)
	  {
		return static_cast<any::holder<ValueType> *>(operand.content)->held;
	  }		    

private:

	void _destroy()
	{
		if(counter && content && --*counter <= 0)
		{
			delete counter;
			delete content;
		}
	}

   placeholder *content;
   int         *counter;

};

template<typename ValueType>
ValueType any_cast(const any & operand) 
{
	return any::cast<ValueType>(operand);
}

template<typename ValueType>
ValueType any_cast(any & operand) 
{
	return any::cast<ValueType>(operand);
}

}


#endif	

#endif