/*************************************************************************************************/
/**
	expression.cpp

	Contains all the LineParser methods for parsing and evaluating expressions


	Copyright (C) Rich Talbot-Watkins 2007 - 2012

	This file is part of BeebAsm.

	BeebAsm is free software: you can redistribute it and/or modify it under the terms of the GNU
	General Public License as published by the Free Software Foundation, either version 3 of the
	License, or (at your option) any later version.

	BeebAsm is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without
	even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License along with BeebAsm, as
	COPYING.txt.  If not, see <http://www.gnu.org/licenses/>.
*/
/*************************************************************************************************/

#include <iostream>
#include <cmath>
#include <cstring>
#include <cerrno>
#include <sstream>
#include <iomanip>
#include <climits>

#include "lineparser.h"
#include "asmexception.h"
#include "symboltable.h"
#include "globaldata.h"
#include "objectcode.h"
#include "sourcefile.h"
#include "random.h"
#include "constants.h"
#include "stringutils.h"
#include "literals.h"

using namespace std;


// Expand a string into the string and its length
#define N(n) n,sizeof(n)-1

const LineParser::Operator	LineParser::m_gaBinaryOperatorTable[] =
{
	{ N(")"),		-1,	0,	NULL },		// special case
	{ N("]"),		-1,	0,	NULL },		// special case
	{ N(","),		-1,	0,	NULL },		// special case

	{ N("^"),		7,	0,	&LineParser::EvalPower },
	{ N("*"),		6,	0,	&LineParser::EvalMultiply },
	{ N("/"),		6,	0,	&LineParser::EvalDivide },
	{ N("%"),		6,	0,	&LineParser::EvalMod },
	{ N("DIV"),		6,	0,	&LineParser::EvalDiv },
	{ N("MOD"),		6,	0,	&LineParser::EvalMod },
	{ N("<<"),		6,	0,	&LineParser::EvalShiftLeft },
	{ N(">>"),		6,	0,	&LineParser::EvalShiftRight },
	{ N("+"),		5,	0,	&LineParser::EvalAdd },
	{ N("-"),		5,	0,	&LineParser::EvalSubtract },
	{ N("=="),		4,	0,	&LineParser::EvalEqual },
	{ N("="),		4,	0,	&LineParser::EvalEqual },
	{ N("<>"),		4,	0,	&LineParser::EvalNotEqual },
	{ N("!="),		4,	0,	&LineParser::EvalNotEqual },
	{ N("<="),		4,	0,	&LineParser::EvalLessThanOrEqual },
	{ N(">="),		4,	0,	&LineParser::EvalMoreThanOrEqual },
	{ N("<"),		4,	0,	&LineParser::EvalLessThan },
	{ N(">"),		4,	0,	&LineParser::EvalMoreThan },
	{ N("AND"),		3,	0,	&LineParser::EvalAnd },
	{ N("OR"),		2,	0,	&LineParser::EvalOr },
	{ N("EOR"),		2,	0,	&LineParser::EvalEor }
};



const LineParser::Operator	LineParser::m_gaUnaryOperatorTable[] =
{
	{ N("("),		-1,	0, NULL },		// special case
	{ N("["),		-1,	0, NULL },		// special case

	{ N("-"),		8,	0,	&LineParser::EvalNegate },
	{ N("+"),		8,	0,	&LineParser::EvalPosate },
	{ N("HI("),		10,	1,	&LineParser::EvalHi },
	{ N("LO("),		10,	1,	&LineParser::EvalLo },
	{ N(">"),		10,	0,	&LineParser::EvalHi },
	{ N("<"),		10,	0,	&LineParser::EvalLo },
	{ N("SIN("),	10, 1,	&LineParser::EvalSin },
	{ N("COS("),	10, 1,	&LineParser::EvalCos },
	{ N("TAN("),	10, 1,	&LineParser::EvalTan },
	{ N("ASN("),	10, 1,	&LineParser::EvalArcSin },
	{ N("ACS("),	10, 1,	&LineParser::EvalArcCos },
	{ N("ATN("),	10, 1,	&LineParser::EvalArcTan },
	{ N("SQR("),	10, 1,	&LineParser::EvalSqrt },
	{ N("RAD("),	10, 1,	&LineParser::EvalDegToRad },
	{ N("DEG("),	10, 1,	&LineParser::EvalRadToDeg },
	{ N("INT("),	10,	1,	&LineParser::EvalInt },
	{ N("ABS("),	10, 1,	&LineParser::EvalAbs },
	{ N("SGN("),	10, 1,	&LineParser::EvalSgn },
	{ N("RND("),	10,	1,	&LineParser::EvalRnd },
	{ N("NOT("),	10, 1,	&LineParser::EvalNot },
	{ N("LOG("),	10, 1,	&LineParser::EvalLog },
	{ N("LN("),		10,	1,	&LineParser::EvalLn },
	{ N("EXP("),	10,	1,	&LineParser::EvalExp },
	{ N("TIME$("),	10,	1,	&LineParser::EvalTime },
	{ N("STR$("),	10,	1,	&LineParser::EvalStr },
	{ N("STR$~("),	10,	1,	&LineParser::EvalStrHex },
	{ N("VAL("),	10,	1,	&LineParser::EvalVal },
	{ N("EVAL("),	10,	1,	&LineParser::EvalEval },
	{ N("LEN("),	10,	1,	&LineParser::EvalLen },
	{ N("CHR$("),	10,	1,	&LineParser::EvalChr },
	{ N("ASC("),	10,	1,	&LineParser::EvalAsc },
	{ N("MID$("),	10,	3,	&LineParser::EvalMid },
	{ N("LEFT$("),	10,	2,	&LineParser::EvalLeft },
	{ N("RIGHT$("),	10,	2,	&LineParser::EvalRight },
	{ N("STRING$("),10,	2,	&LineParser::EvalString },
	{ N("UPPER$("),	10,	1,	&LineParser::EvalUpper },
	{ N("LOWER$("),	10,	1,	&LineParser::EvalLower }
};

#undef N



/*************************************************************************************************/
/**
	LineParser::GetValue()

	Parses a simple value.  This may be
	- a decimal literal
	- a hex literal (prefixed by &)
	- a string
	- a symbol (label)
	- a special value such as * (PC)

	@return		double
*/
/*************************************************************************************************/
Value LineParser::GetValue()
{
	Value value;

	double double_value;
	if ( Literals::ParseNumeric(m_line, m_column, double_value) )
	{
		value = double_value;
	}
	else if ( m_column < m_line.length() && m_line[ m_column ] == '*' )
	{
		// get current PC

		m_column++;
		value = static_cast< double >( ObjectCode::Instance().GetPC() );
	}
	else if ( m_column < m_line.length() && m_line[ m_column ] == '\'' )
	{
		// get char literal

		if ( m_line.length() - m_column < 3 ||  m_line[ m_column + 2 ] != '\'' )
		{
			// bad syntax - must be e.g. 'A'
			throw AsmException_SyntaxError_InvalidCharacter( m_line, m_column );
		}

		value = static_cast< double >( m_line[ m_column + 1 ] );
		m_column += 3;
	}
	else if ( m_column < m_line.length() && m_line[ m_column ] == '\"' )
	{
		// get string literal

		std::vector<char> text;
		m_column++;
		bool done = false;
		while (!done && (m_column < m_line.length()))
		{
			char c = m_line[ m_column ];
			m_column++;
			if (c == '\"')
			{
				if ((m_column < m_line.length()) && (m_line[m_column] == '\"'))
				{
					// Quote quoted by doubling
					text.push_back(c);
					m_column++;
				}
				else
				{
					done = true;
				}
			}
			else
			{
				text.push_back(c);
			}
		}
		if (!done)
		{
			throw AsmException_SyntaxError_MissingQuote( m_line, m_line.length() );
		}
		value = String(text.data(), text.size());
	}
	else if ( m_column < m_line.length() && ( Ascii::IsAlpha( m_line[ m_column ] ) || m_line[ m_column ] == '_' ) )
	{
		// get a symbol

		int oldColumn = m_column;
		string symbolName = GetSymbolName();

		if (symbolName == "TIME$")
		{
			// Handle TIME$ with no parameters
			value = FormatAssemblyTime("%a,%d %b %Y.%H:%M:%S");
		}
		else
		{
			// Regular symbol

			if ( !m_sourceCode->GetSymbolValue(symbolName, value) )
			{
				// symbol not known
				throw AsmException_SyntaxError_SymbolNotDefined( m_line, oldColumn );
			}
		}
	}
	else
	{
		// expected value
		throw AsmException_SyntaxError_InvalidCharacter( m_line, m_column );
	}

	return value;
}



/*************************************************************************************************/
/**
	LineParser::EvaluateExpression()

	Evaluates an expression, and returns its value, also advancing the string pointer
*/
/*************************************************************************************************/
Value LineParser::EvaluateExpression( bool bAllowOneMismatchedCloseBracket )
{
	// Reset stacks

	m_valueStackPtr = 0;
	m_operatorStackPtr = 0;

	// Count brackets

	int bracketCount = 0;

	// When we know a '(' is coming (because it was the end of a token) this is the number of commas to expect
	// in the parameter list, i.e. one less than the number of parameters.
	int pendingCommaCount = 0;

	TYPE expected = VALUE_OR_UNARY;

	// Iterate through the expression

	while ( AdvanceAndCheckEndOfSubStatement(bracketCount == 0) )
	{
		if ( expected == VALUE_OR_UNARY )
		{
			// Look for unary operator

			int matchedToken = -1;

			// Check against unary operator tokens

			for ( unsigned int i = 0; i < sizeof m_gaUnaryOperatorTable / sizeof(Operator); i++ )
			{
				const char*		token	= m_gaUnaryOperatorTable[ i ].token;
				size_t			len		= m_gaUnaryOperatorTable[ i ].m_tokenLength;
				assert(len == strlen( token ));
				// see if token matches

				bool bMatch = true;
				for ( unsigned int j = 0; j < len; j++ )
				{
					if ( ( m_column + j >= m_line.length() ) || ( token[ j ] != Ascii::ToUpper( m_line[ m_column + j ] ) ) )
					{
						bMatch = false;
						break;
					}
				}

				// it matches; advance line pointer and remember token

				if ( bMatch )
				{
					matchedToken = i;
					m_column += len;

					// if token ends with (but is not) an open bracket, step backwards one place so that we parse it next time

					if ( len > 1 && token[ len - 1 ] == '(' )
					{
						pendingCommaCount = m_gaUnaryOperatorTable[ matchedToken ].parameterCount - 1;
						m_column--;
						assert( m_line[ m_column ] == '(' );
					}

					break;
				}
			}

			if ( matchedToken == -1 )
			{
				// If unary operator not found, look for a value instead

				if ( m_valueStackPtr == MAX_VALUES )
				{
					throw AsmException_SyntaxError_ExpressionTooComplex( m_line, m_column );
				}

				Value value;

				try
				{
					value = GetValue();
				}
				catch ( AsmException_SyntaxError_SymbolNotDefined& )
				{
					// If we encountered an unknown symbol whilst evaluating the expression...

					if ( GlobalData::Instance().IsFirstPass() )
					{
						// On first pass, we have to continue gracefully.
						// This moves the string pointer to beyond the expression

						SkipExpression( bracketCount, bAllowOneMismatchedCloseBracket );
					}

					// Whatever happens, we throw the exception
					throw;
				}

				m_valueStack[ m_valueStackPtr++ ] = value;
				expected = BINARY;
			}
			else
			{
				// If unary operator *was* found...

				Operator thisOp = m_gaUnaryOperatorTable[ matchedToken ];

				if ( thisOp.handler != NULL )
				{
					// not an open bracket - we may have to juggle the stack

					while ( m_operatorStackPtr > 0 &&
							thisOp.precedence < m_operatorStack[ m_operatorStackPtr - 1 ].precedence )
					{
						m_operatorStackPtr--;

						OperatorHandler opHandler = m_operatorStack[ m_operatorStackPtr ].handler;
						assert( opHandler != NULL );	// this should really not be possible!

						( this->*opHandler )();
					}
				}
				else
				{
					// The open bracket's parameterCount counts down the number of commas expected.
					thisOp.parameterCount = pendingCommaCount;
					pendingCommaCount = 0;
					bracketCount++;
				}

				if ( m_operatorStackPtr == MAX_OPERATORS )
				{
					throw AsmException_SyntaxError_ExpressionTooComplex( m_line, m_column );
				}

				m_operatorStack[ m_operatorStackPtr++ ] = thisOp;
			}

		}
		else
		{
			// Get binary operator

			int matchedToken = -1;

			for ( unsigned int i = 0; i < sizeof m_gaBinaryOperatorTable / sizeof(Operator); i++ )
			{
				const char*		token	= m_gaBinaryOperatorTable[ i ].token;
				size_t			len		= m_gaBinaryOperatorTable[ i ].m_tokenLength;
				assert(len == strlen( token ));

				// see if token matches

				bool bMatch = true;
				for ( unsigned int j = 0; j < len; j++ )
				{
					if ( ( m_column + j >= m_line.length() ) || ( token[ j ] != Ascii::ToUpper( m_line[ m_column + j ] ) ) )
					{
						bMatch = false;
						break;
					}
				}

				if ( bMatch )
				{
					matchedToken = i;
					m_column += len;
					break;
				}
			}

			if ( matchedToken == -1 )
			{
				// unrecognised binary op
				throw AsmException_SyntaxError_InvalidCharacter( m_line, m_column );
			}

			// we found binary operator

			const Operator& thisOp = m_gaBinaryOperatorTable[ matchedToken ];

			if ( thisOp.handler != NULL )
			{
				// not an close bracket

				while ( m_operatorStackPtr > 0 &&
						thisOp.precedence <= m_operatorStack[ m_operatorStackPtr - 1 ].precedence )
				{
					m_operatorStackPtr--;

					OperatorHandler opHandler = m_operatorStack[ m_operatorStackPtr ].handler;
					assert( opHandler != NULL );	// this means the operator has been given a precedence of < 0

					( this->*opHandler )();
				}

				if ( m_operatorStackPtr == MAX_OPERATORS )
				{
					throw AsmException_SyntaxError_ExpressionTooComplex( m_line, m_column );
				}

				m_operatorStack[ m_operatorStackPtr++ ] = thisOp;

				expected = VALUE_OR_UNARY;
			}
			else
			{
				// is a close bracket or parameter separator

				bool separator = strcmp(thisOp.token, ",") == 0;

				if (!separator)
				{
					bracketCount--;
				}

				bool bFoundMatchingBracket = false;

				while ( m_operatorStackPtr > 0 )
				{
					m_operatorStackPtr--;

					OperatorHandler opHandler = m_operatorStack[ m_operatorStackPtr ].handler;
					if ( opHandler != NULL )
					{
						( this->*opHandler )();
					}
					else
					{
						bFoundMatchingBracket = true;
						break;
					}
				}

				if ( bFoundMatchingBracket )
				{
					if (separator)
					{
						// parameter separator

						// check we are expecting multiple parameters
						if (m_operatorStack[ m_operatorStackPtr ].parameterCount == 0)
						{
							throw AsmException_SyntaxError_ParameterCount( m_line, m_column - 1 );
						}
						m_operatorStack[ m_operatorStackPtr ].parameterCount--;

						// put the open bracket back on the stack
						m_operatorStackPtr++;

						// expect the next parameter
						expected = VALUE_OR_UNARY;
					}
					else
					{
						// close par

						// check all parameters have been supplied
						if (m_operatorStack[ m_operatorStackPtr ].parameterCount != 0)
						{
							throw AsmException_SyntaxError_ParameterCount( m_line, m_column - 1 );
						}
					}
				}
				else
				{
					// did not find matching bracket
					if ( bAllowOneMismatchedCloseBracket )
					{
						// this is a hack which allows an extra close bracket to terminate an expression,
						// so that we can parse LDA (ind),Y and JMP (ind) where the open bracket is not
						// included in the expression

						m_column--;
						break;	// jump out of the loop, ready to exit
					}
					else
					{
						// mismatched brackets
						throw AsmException_SyntaxError_MismatchedParentheses( m_line, m_column - 1 );
					}
				}
			}
		}
	}

	// purge the operator stack

	while ( m_operatorStackPtr > 0 )
	{
		m_operatorStackPtr--;

		OperatorHandler opHandler = m_operatorStack[ m_operatorStackPtr ].handler;

		if ( opHandler == NULL )
		{
			// mismatched brackets
			throw AsmException_SyntaxError_MismatchedParentheses( m_line, m_column );
		}
		else
		{
			( this->*opHandler )();
		}
	}

	assert( m_valueStackPtr <= 1 );

	if ( m_valueStackPtr == 0 )
	{
		// nothing was found
		throw AsmException_SyntaxError_EmptyExpression( m_line, m_column );
	}

	return m_valueStack[ 0 ];
}

/*************************************************************************************************/
/**
	LineParser::EvaluateExpressionAsDouble()

	Version of EvaluateExpression which returns its result as a double or throws a type mismatch
*/
/*************************************************************************************************/
double LineParser::EvaluateExpressionAsDouble( bool bAllowOneMismatchedCloseBracket )
{
	Value value = EvaluateExpression( bAllowOneMismatchedCloseBracket );
	if (value.GetType() != Value::NumberValue)
	{
		throw AsmException_SyntaxError_TypeMismatch( m_line, m_column );
	}
	return value.GetNumber();
}


/*************************************************************************************************/
/**
	LineParser::EvaluateExpressionAsInt()

	Version of EvaluateExpression which returns its result as an int
*/
/*************************************************************************************************/
int LineParser::EvaluateExpressionAsInt( bool bAllowOneMismatchedCloseBracket )
{
	return ConvertDoubleToInt( EvaluateExpressionAsDouble( bAllowOneMismatchedCloseBracket ) );
}


/*************************************************************************************************/
/**
	LineParser::EvaluateExpressionAsUnsignedInt()

	Version of EvaluateExpression which returns its result as an unsigned int
*/
/*************************************************************************************************/
unsigned int LineParser::EvaluateExpressionAsUnsignedInt( bool bAllowOneMismatchedCloseBracket )
{
	return static_cast< unsigned int >( ConvertDoubleToInt( EvaluateExpressionAsDouble( bAllowOneMismatchedCloseBracket ) ) );
}


/*************************************************************************************************/
/**
	LineParser::EvaluateExpressionAsString()

	Version of EvaluateExpression which returns its result as a String or throws a type mismatch
*/
/*************************************************************************************************/
 string LineParser::EvaluateExpressionAsString( bool bAllowOneMismatchedCloseBracket )
{
	Value value = EvaluateExpression( bAllowOneMismatchedCloseBracket );
	if (value.GetType() != Value::StringValue)
	{
		throw AsmException_SyntaxError_TypeMismatch( m_line, m_column );
	}
	String result = value.GetString();
	return string(result.Text(), result.Length());
}


/*************************************************************************************************/
/**
	LineParser::StackTopTwoValues()

	Retrieve two values of matching type, or throw an exception
*/
/*************************************************************************************************/
std::pair<Value, Value> LineParser::StackTopTwoValues()
{
	if ( m_valueStackPtr < 2 )
	{
		throw AsmException_SyntaxError_MissingValue( m_line, m_column );
	}
	Value value1 = m_valueStack[ m_valueStackPtr - 2 ];
	Value value2 = m_valueStack[ m_valueStackPtr - 1 ];
	if (value1.GetType() != value2.GetType())
	{
		throw AsmException_SyntaxError_TypeMismatch( m_line, m_column );
	}
	return std::pair<Value, Value>(value1, value2);
}


/*************************************************************************************************/
/**
	LineParser::StackTopString()

	Retrieve a string from the top of the stack, or throw an exception
*/
/*************************************************************************************************/
String LineParser::StackTopString()
{
	if ( m_valueStackPtr < 1 )
	{
		throw AsmException_SyntaxError_MissingValue( m_line, m_column );
	}
	Value value = m_valueStack[ m_valueStackPtr - 1 ];
	if (value.GetType() != Value::StringValue)
	{
		throw AsmException_SyntaxError_TypeMismatch( m_line, m_column );
	}
	return value.GetString();
}


/*************************************************************************************************/
/**
	LineParser::StackTopNumber()

	Retrieve a number from the top of the stack, or throw an exception
*/
/*************************************************************************************************/
double LineParser::StackTopNumber()
{
	if ( m_valueStackPtr < 1 )
	{
		throw AsmException_SyntaxError_MissingValue( m_line, m_column );
	}
	Value value = m_valueStack[ m_valueStackPtr - 1 ];
	if (value.GetType() != Value::NumberValue)
	{
		throw AsmException_SyntaxError_TypeMismatch( m_line, m_column );
	}
	return value.GetNumber();
}


/*************************************************************************************************/
/**
	LineParser::StackTopInt()

	Retrieve a number from the top of the stack and convert to an int, or throw an exception
*/
/*************************************************************************************************/
int LineParser::StackTopInt()
{
	return ConvertDoubleToInt( StackTopNumber() );
}


/*************************************************************************************************/
/**
	LineParser::StackTopTwoNumbers()

	Retrieve two values of numeric type, or throw an exception
*/
/*************************************************************************************************/
std::pair<double, double> LineParser::StackTopTwoNumbers()
{
	if ( m_valueStackPtr < 2 )
	{
		throw AsmException_SyntaxError_MissingValue( m_line, m_column );
	}
	Value value1 = m_valueStack[ m_valueStackPtr - 2 ];
	Value value2 = m_valueStack[ m_valueStackPtr - 1 ];
	if ((value1.GetType() != Value::NumberValue) || (value2.GetType() != Value::NumberValue))
	{
		throw AsmException_SyntaxError_TypeMismatch( m_line, m_column );
	}
	return std::pair<double, double>(value1.GetNumber(), value2.GetNumber());
}


/*************************************************************************************************/
/**
	LineParser::StackTopTwoInts()

	Retrieve two values of numeric type and convert to ints, or throw an exception
*/
/*************************************************************************************************/
std::pair<int, int> LineParser::StackTopTwoInts()
{
	std::pair<double, double> pair = StackTopTwoNumbers();
	return std::pair<int, int>(ConvertDoubleToInt(pair.first), ConvertDoubleToInt(pair.second));
}

/*************************************************************************************************/
/**
	LineParser::ConvertDoubleToInt()

	Convert a double to an int throwing an exception if it is out of range

	Handle values between -2147483648 and 4294967295.
*/
/*************************************************************************************************/
int LineParser::ConvertDoubleToInt(double value)
{
	if ((value < INT_MIN) || (value > UINT_MAX))
	{
		throw AsmException_SyntaxError_OutOfIntegerRange( m_line, m_column );
	}

	if (value <= INT_MAX)
	{
		return static_cast<int>( value );
	}
	else
	{
		return static_cast<unsigned int>( value );
	}
}

/*************************************************************************************************/
/**
	LineParser::EvalAdd()
*/
/*************************************************************************************************/
void LineParser::EvalAdd()
{
	std::pair<Value, Value> values = StackTopTwoValues();

	if (values.first.GetType() == Value::NumberValue)
	{
		m_valueStack[ m_valueStackPtr - 2 ] = Value(values.first.GetNumber() + values.second.GetNumber());
	}
	else if (values.first.GetType() == Value::StringValue)
	{
		m_valueStack[ m_valueStackPtr - 2 ] = Value(values.first.GetString() + values.second.GetString());
	}
	m_valueStackPtr--;
}

/*************************************************************************************************/
/**
	LineParser::EvalSubtract()
*/
/*************************************************************************************************/
void LineParser::EvalSubtract()
{
	std::pair<double, double> values = StackTopTwoNumbers();
	m_valueStack[ m_valueStackPtr - 2 ] = values.first - values.second;
	m_valueStackPtr--;
}



/*************************************************************************************************/
/**
	LineParser::EvalMultiply()
*/
/*************************************************************************************************/
void LineParser::EvalMultiply()
{
	std::pair<double, double> values = StackTopTwoNumbers();
	m_valueStack[ m_valueStackPtr - 2 ] = values.first * values.second;
	m_valueStackPtr--;
}



/*************************************************************************************************/
/**
	LineParser::EvalDivide()
*/
/*************************************************************************************************/
void LineParser::EvalDivide()
{
	std::pair<double, double> values = StackTopTwoNumbers();
	if ( values.second == 0.0 )
	{
		throw AsmException_SyntaxError_DivisionByZero( m_line, m_column - 1 );
	}
	m_valueStack[ m_valueStackPtr - 2 ] = values.first / values.second;
	m_valueStackPtr--;
}



/*************************************************************************************************/
/**
	LineParser::EvalPower()
*/
/*************************************************************************************************/
void LineParser::EvalPower()
{
	std::pair<double, double> values = StackTopTwoNumbers();
	m_valueStack[ m_valueStackPtr - 2 ] = pow( values.first, values.second );
	m_valueStackPtr--;

	if ( errno == ERANGE )
	{
		throw AsmException_SyntaxError_NumberTooBig( m_line, m_column - 1 );
	}

	if ( errno == EDOM )
	{
		throw AsmException_SyntaxError_IllegalOperation( m_line, m_column - 1 );
	}
}



/*************************************************************************************************/
/**
	LineParser::EvalDiv()
*/
/*************************************************************************************************/
void LineParser::EvalDiv()
{
	std::pair<int, int> values = StackTopTwoInts();

	if ( values.second == 0 )
	{
		throw AsmException_SyntaxError_DivisionByZero( m_line, m_column - 1 );
	}
	m_valueStack[ m_valueStackPtr - 2 ] = static_cast< double >(values.first / values.second);
	m_valueStackPtr--;
}



/*************************************************************************************************/
/**
	LineParser::EvalMod()
*/
/*************************************************************************************************/
void LineParser::EvalMod()
{
	std::pair<int, int> values = StackTopTwoInts();

	if ( values.second == 0 )
	{
		throw AsmException_SyntaxError_DivisionByZero( m_line, m_column - 1 );
	}
	m_valueStack[ m_valueStackPtr - 2 ] = static_cast< double >(values.first % values.second);
	m_valueStackPtr--;
}



/*************************************************************************************************/
/**
	Shifting helper functions
*/
/*************************************************************************************************/
static int LogicalShiftLeft(int value, unsigned int shift)
{
	return static_cast<int>(static_cast<unsigned int>(value) << shift);
}

static int ArithmeticShiftRight(int value, unsigned int shift)
{
	unsigned int shifted = static_cast<unsigned int>(value) >> shift;
	if (value < 0)
	{
		const unsigned int bitcount = 8 * sizeof(unsigned int);
		const unsigned int ones = ~0;
		shifted |= (ones << (bitcount - shift));
	}
	return static_cast<int>(shifted);
}

/*************************************************************************************************/
/**
	LineParser::EvalShiftLeft()
*/
/*************************************************************************************************/
void LineParser::EvalShiftLeft()
{
	std::pair<int, int> values = StackTopTwoInts();

	int val = values.first;
	int shift = values.second;
	int result;

	if ( shift > 31 || shift < -31 )
	{
		result = 0;
	}
	else if ( shift > 0 )
	{
		result = LogicalShiftLeft(val, static_cast<unsigned int>(shift));
	}
	else if ( shift == 0 )
	{
		result = val;
	}
	else
	{
		result = ArithmeticShiftRight(val, static_cast<unsigned int>(-shift));
	}

	m_valueStack[ m_valueStackPtr - 2 ] = static_cast< double >( result );
	m_valueStackPtr--;
}



/*************************************************************************************************/
/**
	LineParser::EvalShiftRight()
*/
/*************************************************************************************************/
void LineParser::EvalShiftRight()
{
	std::pair<int, int> values = StackTopTwoInts();

	int val = values.first;
	int shift = values.second;
	int result;

	if ( shift > 31 || shift < -31 )
	{
		result = 0;
	}
	else if ( shift > 0 )
	{
		result = ArithmeticShiftRight(val, static_cast<unsigned int>(shift));
	}
	else if ( shift == 0 )
	{
		result = val;
	}
	else
	{
		result = LogicalShiftLeft(val, static_cast<unsigned int>(-shift));
	}

	m_valueStack[ m_valueStackPtr - 2 ] = static_cast< double >( result );
	m_valueStackPtr--;
}



/*************************************************************************************************/
/**
	LineParser::EvalAnd()
*/
/*************************************************************************************************/
void LineParser::EvalAnd()
{
	std::pair<int, int> values = StackTopTwoInts();
	m_valueStack[ m_valueStackPtr - 2 ] = static_cast< double >(values.first & values.second);
	m_valueStackPtr--;
}



/*************************************************************************************************/
/**
	LineParser::EvalOr()
*/
/*************************************************************************************************/
void LineParser::EvalOr()
{
	std::pair<int, int> values = StackTopTwoInts();
	m_valueStack[ m_valueStackPtr - 2 ] = static_cast< double >(values.first | values.second);
	m_valueStackPtr--;
}



/*************************************************************************************************/
/**
	LineParser::EvalEor()
*/
/*************************************************************************************************/
void LineParser::EvalEor()
{
	std::pair<int, int> values = StackTopTwoInts();
	m_valueStack[ m_valueStackPtr - 2 ] = static_cast< double >(values.first ^ values.second);
	m_valueStackPtr--;
}



/*************************************************************************************************/
/**
	LineParser::EvalEqual()
*/
/*************************************************************************************************/
void LineParser::EvalEqual()
{
	std::pair<Value, Value> values = StackTopTwoValues();
	m_valueStack[ m_valueStackPtr - 2 ] = (Value::Compare(values.first, values.second) == 0) ? -1 : 0;
	m_valueStackPtr--;
}



/*************************************************************************************************/
/**
	LineParser::EvalNotEqual()
*/
/*************************************************************************************************/
void LineParser::EvalNotEqual()
{
	std::pair<Value, Value> values = StackTopTwoValues();
	m_valueStack[ m_valueStackPtr - 2 ] = (Value::Compare(values.first, values.second) != 0) ? -1 : 0;
	m_valueStackPtr--;
}



/*************************************************************************************************/
/**
	LineParser::EvalLessThanOrEqual()
*/
/*************************************************************************************************/
void LineParser::EvalLessThanOrEqual()
{
	std::pair<Value, Value> values = StackTopTwoValues();
	m_valueStack[ m_valueStackPtr - 2 ] = (Value::Compare(values.first, values.second) <= 0) ? -1 : 0;
	m_valueStackPtr--;
}



/*************************************************************************************************/
/**
	LineParser::EvalMoreThanOrEqual()
*/
/*************************************************************************************************/
void LineParser::EvalMoreThanOrEqual()
{
	std::pair<Value, Value> values = StackTopTwoValues();
	m_valueStack[ m_valueStackPtr - 2 ] = (Value::Compare(values.first, values.second) >= 0) ? -1 : 0;
	m_valueStackPtr--;
}



/*************************************************************************************************/
/**
	LineParser::EvalLessThan()
*/
/*************************************************************************************************/
void LineParser::EvalLessThan()
{
	std::pair<Value, Value> values = StackTopTwoValues();
	m_valueStack[ m_valueStackPtr - 2 ] = (Value::Compare(values.first, values.second) < 0) ? -1 : 0;
	m_valueStackPtr--;
}



/*************************************************************************************************/
/**
	LineParser::EvalMoreThan()
*/
/*************************************************************************************************/
void LineParser::EvalMoreThan()
{
	std::pair<Value, Value> values = StackTopTwoValues();
	m_valueStack[ m_valueStackPtr - 2 ] = (Value::Compare(values.first, values.second) > 0) ? -1 : 0;
	m_valueStackPtr--;
}



/*************************************************************************************************/
/**
	LineParser::EvalNegate()
*/
/*************************************************************************************************/
void LineParser::EvalNegate()
{
	m_valueStack[ m_valueStackPtr - 1 ] = -StackTopNumber();
}



/*************************************************************************************************/
/**
	LineParser::EvalNot()
*/
/*************************************************************************************************/
void LineParser::EvalNot()
{
	int value = ~StackTopInt();
	m_valueStack[ m_valueStackPtr - 1 ] = static_cast< double >(value);
}



/*************************************************************************************************/
/**
	LineParser::EvalPosate()
*/
/*************************************************************************************************/
void LineParser::EvalPosate()
{
	if ( m_valueStackPtr < 1 )
	{
		throw AsmException_SyntaxError_MissingValue( m_line, m_column );
	}
	// does absolutely nothing
}



/*************************************************************************************************/
/**
	LineParser::EvalLo()
*/
/*************************************************************************************************/
void LineParser::EvalLo()
{
	int value = StackTopInt() & 0xFF;
	m_valueStack[ m_valueStackPtr - 1 ] = static_cast< double >(value);
}



/*************************************************************************************************/
/**
	LineParser::EvalHi()
*/
/*************************************************************************************************/
void LineParser::EvalHi()
{
	int value = (StackTopInt() & 0xffff) >> 8;
	m_valueStack[ m_valueStackPtr - 1 ] = static_cast< double >(value);
}



/*************************************************************************************************/
/**
	LineParser::EvalSin()
*/
/*************************************************************************************************/
void LineParser::EvalSin()
{
	m_valueStack[ m_valueStackPtr - 1 ] = sin( StackTopNumber() );
}



/*************************************************************************************************/
/**
	LineParser::EvalCos()
*/
/*************************************************************************************************/
void LineParser::EvalCos()
{
	m_valueStack[ m_valueStackPtr - 1 ] = cos( StackTopNumber() );
}



/*************************************************************************************************/
/**
	LineParser::EvalTan()
*/
/*************************************************************************************************/
void LineParser::EvalTan()
{
	m_valueStack[ m_valueStackPtr - 1 ] = tan( StackTopNumber() );
}



/*************************************************************************************************/
/**
	LineParser::EvalArcSin()
*/
/*************************************************************************************************/
void LineParser::EvalArcSin()
{
	m_valueStack[ m_valueStackPtr - 1 ] = asin( StackTopNumber() );

	if ( errno == EDOM )
	{
		throw AsmException_SyntaxError_IllegalOperation( m_line, m_column - 1 );
	}
}



/*************************************************************************************************/
/**
	LineParser::EvalArcCos()
*/
/*************************************************************************************************/
void LineParser::EvalArcCos()
{
	m_valueStack[ m_valueStackPtr - 1 ] = acos( StackTopNumber() );

	if ( errno == EDOM )
	{
		throw AsmException_SyntaxError_IllegalOperation( m_line, m_column - 1 );
	}
}



/*************************************************************************************************/
/**
	LineParser::EvalArcTan()
*/
/*************************************************************************************************/
void LineParser::EvalArcTan()
{
	m_valueStack[ m_valueStackPtr - 1 ] = atan( StackTopNumber() );

	if ( errno == EDOM )
	{
		throw AsmException_SyntaxError_IllegalOperation( m_line, m_column - 1 );
	}
}



/*************************************************************************************************/
/**
	LineParser::EvalLog()
*/
/*************************************************************************************************/
void LineParser::EvalLog()
{
	m_valueStack[ m_valueStackPtr - 1 ] = log10( StackTopNumber() );

	if ( errno == EDOM || errno == ERANGE )
	{
		throw AsmException_SyntaxError_IllegalOperation( m_line, m_column - 1 );
	}
}



/*************************************************************************************************/
/**
	LineParser::EvalLn()
*/
/*************************************************************************************************/
void LineParser::EvalLn()
{
	m_valueStack[ m_valueStackPtr - 1 ] = log( StackTopNumber() );

	if ( errno == EDOM || errno == ERANGE )
	{
		throw AsmException_SyntaxError_IllegalOperation( m_line, m_column - 1 );
	}
}



/*************************************************************************************************/
/**
	LineParser::EvalExp()
*/
/*************************************************************************************************/
void LineParser::EvalExp()
{
	m_valueStack[ m_valueStackPtr - 1 ] = exp( StackTopNumber() );

	if ( errno == ERANGE )
	{
		throw AsmException_SyntaxError_IllegalOperation( m_line, m_column - 1 );
	}
}



/*************************************************************************************************/
/**
	LineParser::EvalSqrt()
*/
/*************************************************************************************************/
void LineParser::EvalSqrt()
{
	m_valueStack[ m_valueStackPtr - 1 ] = sqrt( StackTopNumber() );

	if ( errno == EDOM )
	{
		throw AsmException_SyntaxError_IllegalOperation( m_line, m_column - 1 );
	}
}



/*************************************************************************************************/
/**
	LineParser::EvalDegToRad()
*/
/*************************************************************************************************/
void LineParser::EvalDegToRad()
{
	m_valueStack[ m_valueStackPtr - 1 ] = StackTopNumber() * const_pi / 180.0;
}



/*************************************************************************************************/
/**
	LineParser::EvalRadToDeg()
*/
/*************************************************************************************************/
void LineParser::EvalRadToDeg()
{
	m_valueStack[ m_valueStackPtr - 1 ] = StackTopNumber() * 180.0 / const_pi;
}



/*************************************************************************************************/
/**
	LineParser::EvalInt()
*/
/*************************************************************************************************/
void LineParser::EvalInt()
{
	m_valueStack[ m_valueStackPtr - 1 ] = static_cast< double >( StackTopInt() );
}



/*************************************************************************************************/
/**
	LineParser::EvalAbs()
*/
/*************************************************************************************************/
void LineParser::EvalAbs()
{
	m_valueStack[ m_valueStackPtr - 1 ] = abs( StackTopNumber() );
}



/*************************************************************************************************/
/**
	LineParser::EvalSgn()
*/
/*************************************************************************************************/
void LineParser::EvalSgn()
{
	double val = StackTopNumber();
	m_valueStack[ m_valueStackPtr - 1 ] = ( val < 0.0 ) ? -1.0 : ( ( val > 0.0 ) ? 1.0 : 0.0 );
}



/*************************************************************************************************/
/**
	LineParser::EvalRnd()
*/
/*************************************************************************************************/
void LineParser::EvalRnd()
{
	double val = StackTopNumber();
	double result = 0.0;

	if ( val < 1.0f )
	{
		throw AsmException_SyntaxError_IllegalOperation( m_line, m_column - 1 );
	}
	else if ( val == 1.0f )
	{
		result = beebasm_rand() / ( static_cast< double >( BEEBASM_RAND_MAX ) + 1.0 );
	}
	else
	{
		result = static_cast< double >( ConvertDoubleToInt( beebasm_rand() / ( static_cast< double >( BEEBASM_RAND_MAX ) + 1.0 ) * val ) );
	}

	m_valueStack[ m_valueStackPtr - 1 ] = result;
}


/*************************************************************************************************/
/**
	LineParser::EvalTime()
*/
/*************************************************************************************************/
void LineParser::EvalTime()
{
	m_valueStack[ m_valueStackPtr - 1 ] = FormatAssemblyTime(StackTopString().Text());
}


/*************************************************************************************************/
/**
	LineParser::FormatAssemblyTime()

	Format the assembly time using the given strftime format string
*/
/*************************************************************************************************/
Value LineParser::FormatAssemblyTime(const char* formatString)
{
	char timeString[256];
	const time_t t = GlobalData::Instance().GetAssemblyTime();
	const struct tm* t_tm = localtime( &t );
	int length = strftime( timeString, sizeof( timeString ), formatString, t_tm );
	if ( length == 0 )
	{
		throw AsmException_SyntaxError_TimeResultTooBig( m_line, m_column );
	}
	return String(timeString, length);
}


/*************************************************************************************************/
/**
	LineParser::EvalStr()
*/
/*************************************************************************************************/
void LineParser::EvalStr()
{
	ostringstream stream;
	StringUtils::PrintNumber(stream, StackTopNumber());
	string result = stream.str();

	m_valueStack[ m_valueStackPtr - 1 ] = String(result.data(), result.length());
}


/*************************************************************************************************/
/**
	LineParser::EvalStrHex()
*/
/*************************************************************************************************/
void LineParser::EvalStrHex()
{
	ostringstream stream;
	stream << std::hex << std::uppercase << StackTopInt();
	string result = stream.str();

	m_valueStack[ m_valueStackPtr - 1 ] = String(result.data(), result.length());
}


/*************************************************************************************************/
/**
	LineParser::EvalVal()
*/
/*************************************************************************************************/
void LineParser::EvalVal()
{
	String str = StackTopString();
	char* end;
	double value = strtod(str.Text(), &end);

	m_valueStack[ m_valueStackPtr - 1 ] = value;
}


/*************************************************************************************************/
/**
	LineParser::EvalEval()
*/
/*************************************************************************************************/
void LineParser::EvalEval()
{
	String expr = StackTopString();
	LineParser parser(m_sourceCode, string(expr.Text(), expr.Length()));
	Value result = parser.EvaluateExpression();
	m_valueStack[ m_valueStackPtr - 1 ] = result;
}


/*************************************************************************************************/
/**
	LineParser::EvalLen()
*/
/*************************************************************************************************/
void LineParser::EvalLen()
{
	String str = StackTopString();
	m_valueStack[ m_valueStackPtr - 1 ] = str.Length();
}


/*************************************************************************************************/
/**
	LineParser::EvalChr()
*/
/*************************************************************************************************/
void LineParser::EvalChr()
{
	int ascii = StackTopInt();
	if ((ascii < 0) || (ascii > 255))
	{
		throw AsmException_SyntaxError_IllegalOperation( m_line, m_column );
	}
	char buffer = ascii;
	m_valueStack[ m_valueStackPtr - 1 ] = String(&buffer, 1);
}


/*************************************************************************************************/
/**
	LineParser::EvalAsc()
*/
/*************************************************************************************************/
void LineParser::EvalAsc()
{
	String str = StackTopString();
	if (str.Length() == 0)
	{
		throw AsmException_SyntaxError_IllegalOperation( m_line, m_column );
	}

	m_valueStack[ m_valueStackPtr - 1 ] = static_cast<unsigned char>(str[0]);
}


/*************************************************************************************************/
/**
	LineParser::EvalMid()
*/
/*************************************************************************************************/
void LineParser::EvalMid()
{
	if ( m_valueStackPtr < 3 )
	{
		throw AsmException_SyntaxError_MissingValue( m_line, m_column );
	}
	Value value1 = m_valueStack[ m_valueStackPtr - 3 ];
	Value value2 = m_valueStack[ m_valueStackPtr - 2 ];
	Value value3 = m_valueStack[ m_valueStackPtr - 1 ];
	if ((value1.GetType() != Value::StringValue) || (value2.GetType() != Value::NumberValue) || (value3.GetType() != Value::NumberValue))
	{
		throw AsmException_SyntaxError_TypeMismatch( m_line, m_column );
	}
	m_valueStackPtr -= 2;

	String text = value1.GetString();
	int index = ConvertDoubleToInt(value2.GetNumber()) - 1;
	int length = ConvertDoubleToInt(value3.GetNumber());
	if ((index < 0) || (static_cast<unsigned int>(index) > text.Length()) || (length < 0))
	{
		throw AsmException_SyntaxError_IllegalOperation( m_line, m_column );
	}

	m_valueStack[ m_valueStackPtr - 1 ] = text.SubString(index, length);
}


/*************************************************************************************************/
/**
	LineParser::EvalLeft()
*/
/*************************************************************************************************/
void LineParser::EvalLeft()
{
	if ( m_valueStackPtr < 2 )
	{
		throw AsmException_SyntaxError_MissingValue( m_line, m_column );
	}
	Value value1 = m_valueStack[ m_valueStackPtr - 2 ];
	Value value2 = m_valueStack[ m_valueStackPtr - 1 ];
	if ((value1.GetType() != Value::StringValue) || (value2.GetType() != Value::NumberValue))
	{
		throw AsmException_SyntaxError_TypeMismatch( m_line, m_column );
	}
	m_valueStackPtr -= 1;

	String text = value1.GetString();
	int count = ConvertDoubleToInt(value2.GetNumber());
	if ((count < 0) || (static_cast<unsigned int>(count) > text.Length()))
	{
		throw AsmException_SyntaxError_IllegalOperation( m_line, m_column );
	}

	m_valueStack[ m_valueStackPtr - 1 ] = text.SubString(0, count);
}


/*************************************************************************************************/
/**
	LineParser::EvalRight()
*/
/*************************************************************************************************/
void LineParser::EvalRight()
{
	if ( m_valueStackPtr < 2 )
	{
		throw AsmException_SyntaxError_MissingValue( m_line, m_column );
	}
	Value value1 = m_valueStack[ m_valueStackPtr - 2 ];
	Value value2 = m_valueStack[ m_valueStackPtr - 1 ];
	if ((value1.GetType() != Value::StringValue) || (value2.GetType() != Value::NumberValue))
	{
		throw AsmException_SyntaxError_TypeMismatch( m_line, m_column );
	}
	m_valueStackPtr -= 1;

	String text = value1.GetString();
	int count = ConvertDoubleToInt(value2.GetNumber());
	if ((count < 0) || (static_cast<unsigned int>(count) > text.Length()))
	{
		throw AsmException_SyntaxError_IllegalOperation( m_line, m_column );
	}

	unsigned int ucount = static_cast<unsigned int>(count);
	m_valueStack[ m_valueStackPtr - 1 ] = text.SubString(text.Length() - ucount, ucount);
}


/*************************************************************************************************/
/**
	LineParser::EvalString()
*/
/*************************************************************************************************/
void LineParser::EvalString()
{
	if ( m_valueStackPtr < 2 )
	{
		throw AsmException_SyntaxError_MissingValue( m_line, m_column );
	}
	Value value1 = m_valueStack[ m_valueStackPtr - 2 ];
	Value value2 = m_valueStack[ m_valueStackPtr - 1 ];
	if ((value1.GetType() != Value::NumberValue) || (value2.GetType() != Value::StringValue))
	{
		throw AsmException_SyntaxError_TypeMismatch( m_line, m_column );
	}
	m_valueStackPtr -= 1;

	int count = ConvertDoubleToInt(value1.GetNumber());
	String text = value2.GetString();
	if ((count < 0) || (count >= 0x10000) || (text.Length() >= 0x10000) || (static_cast<unsigned int>(count) * text.Length() >= 0x10000))
	{
		throw AsmException_SyntaxError_IllegalOperation( m_line, m_column );
	}

	m_valueStack[ m_valueStackPtr - 1 ] = text.Repeat(count);
}


/*************************************************************************************************/
/**
	LineParser::EvalUpper()
*/
/*************************************************************************************************/
void LineParser::EvalUpper()
{
	m_valueStack[ m_valueStackPtr - 1 ] = StackTopString().Upper();
}


/*************************************************************************************************/
/**
	LineParser::EvalLower()
*/
/*************************************************************************************************/
void LineParser::EvalLower()
{
	m_valueStack[ m_valueStackPtr - 1 ] = StackTopString().Lower();
}
