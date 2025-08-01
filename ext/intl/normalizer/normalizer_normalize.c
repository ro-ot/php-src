/*
   +----------------------------------------------------------------------+
   | This source file is subject to version 3.01 of the PHP license,	  |
   | that is bundled with this package in the file LICENSE, and is		  |
   | available through the world-wide-web at the following url:			  |
   | https://www.php.net/license/3_01.txt                                 |
   | If you did not receive a copy of the PHP license and are unable to   |
   | obtain it through the world-wide-web, please send a note to		  |
   | license@php.net so we can mail you a copy immediately.				  |
   +----------------------------------------------------------------------+
   | Authors: Ed Batutis <ed@batutis.com>								  |
   +----------------------------------------------------------------------+
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "php_intl.h"
#include <unicode/unorm2.h>
#include "normalizer.h"
#include "normalizer_class.h"
#include "intl_convert.h"
#include <unicode/utf8.h>


static const UNormalizer2 *intl_get_normalizer(zend_long form, UErrorCode *err)
{/*{{{*/
	switch (form)
	{
		case NORMALIZER_FORM_C:
			return unorm2_getNFCInstance(err);
			break;
		case NORMALIZER_FORM_D:
			return unorm2_getNFDInstance(err);
			break;
		case NORMALIZER_FORM_KC:
			return unorm2_getNFKCInstance(err);
			break;
		case NORMALIZER_FORM_KD:
			return unorm2_getNFKDInstance(err);
			break;
		case NORMALIZER_FORM_KC_CF:
			return unorm2_getNFKCCasefoldInstance(err);
			break;
	}

	*err = U_ILLEGAL_ARGUMENT_ERROR;
	return NULL;
}/*}}}*/

static int32_t intl_normalize(zend_long form, const UChar *src, int32_t src_len, UChar *dst, int32_t dst_len, UErrorCode *err)
{/*{{{*/
	const UNormalizer2 *norm = intl_get_normalizer(form, err);
	if (U_FAILURE(*err)) {
		return -1;
	}

	return unorm2_normalize(norm, src, src_len, dst, dst_len, err);
}/*}}}*/

static UBool intl_is_normalized(zend_long form, const UChar *uinput, int32_t uinput_len, UErrorCode *err)
{/*{{{*/
	const UNormalizer2 *norm = intl_get_normalizer(form, err);

	if(U_FAILURE(*err)) {
		return false;
	}

	return unorm2_isNormalized(norm, uinput, uinput_len, err);
}/*}}}*/

/* {{{ Normalize a string. */
PHP_FUNCTION( normalizer_normalize )
{
	char*			input = NULL;
	/* form is optional, defaults to FORM_C */
	zend_long	    form = NORMALIZER_DEFAULT;
	size_t			input_len = 0;

	UChar*			uinput = NULL;
	int32_t		    uinput_len = 0;
	int			    expansion_factor = 1;
	UErrorCode		status = U_ZERO_ERROR;

	UChar*			uret_buf = NULL;
	int32_t			uret_len = 0;

	zend_string*    u8str;

	int32_t			size_needed;

	intl_error_reset( NULL );

	/* Parse parameters. */
	if( zend_parse_method_parameters( ZEND_NUM_ARGS(), getThis(), "s|l",
				&input, &input_len, &form ) == FAILURE )
	{
		RETURN_THROWS();
	}

	expansion_factor = 1;

	switch(form) {
		case NORMALIZER_FORM_D:
			expansion_factor = 3;
			break;
		case NORMALIZER_FORM_KD:
			expansion_factor = 3;
			break;
		case NORMALIZER_FORM_C:
		case NORMALIZER_FORM_KC:
		case NORMALIZER_FORM_KC_CF:
			break;
		default:
			zend_argument_value_error(2, "must be a a valid normalization form");
			RETURN_THROWS();
	}

	/*
	 * Normalize string (converting it to UTF-16 first).
	 */

	/* First convert the string to UTF-16. */
	intl_convert_utf8_to_utf16(&uinput, &uinput_len, input, input_len, &status );

	if( U_FAILURE( status ) )
	{
		/* Set global error code. */
		intl_error_set_code( NULL, status );

		/* Set error messages. */
		intl_error_set_custom_msg( NULL, "Error converting input string to UTF-16");
		if (uinput) {
			efree( uinput );
		}
		RETURN_FALSE;
	}


	/* Allocate memory for the destination buffer for normalization */
	uret_len = uinput_len * expansion_factor;
	uret_buf = eumalloc( uret_len + 1 );

	/* normalize */
	size_needed = intl_normalize(form, uinput, uinput_len, uret_buf, uret_len, &status);

	/* Bail out if an unexpected error occurred.
	 * (U_BUFFER_OVERFLOW_ERROR means that *target buffer is not large enough).
	 * (U_STRING_NOT_TERMINATED_WARNING usually means that the input string is empty).
	 */
	if( U_FAILURE(status) && status != U_BUFFER_OVERFLOW_ERROR && status != U_STRING_NOT_TERMINATED_WARNING ) {
		intl_error_set_custom_msg( NULL, "Error normalizing string");
		efree( uret_buf );
		efree( uinput );
		RETURN_FALSE;
	}

	if ( size_needed > uret_len ) {
		/* realloc does not seem to work properly - memory is corrupted
		 * uret_buf =  eurealloc(uret_buf, size_needed + 1);
		 */
		efree( uret_buf );
		uret_buf = eumalloc( size_needed + 1 );
		uret_len = size_needed;

		status = U_ZERO_ERROR;

		/* try normalize again */
		size_needed = intl_normalize(form, uinput, uinput_len, uret_buf, uret_len, &status);

		/* Bail out if an unexpected error occurred. */
		if( U_FAILURE(status)  ) {
			/* Set error messages. */
			intl_error_set_custom_msg( NULL,"Error normalizing string");
			efree( uret_buf );
			efree( uinput );
			RETURN_FALSE;
		}
	}

	efree( uinput );

	/* the buffer we actually used */
	uret_len = size_needed;

	/* Convert normalized string from UTF-16 to UTF-8. */
	u8str = intl_convert_utf16_to_utf8(uret_buf, uret_len, &status );
	efree( uret_buf );
	if( !u8str )
	{
		intl_error_set( NULL, status,
				"error converting normalized text UTF-8");
		RETURN_FALSE;
	}

	/* Return it. */
	RETVAL_NEW_STR( u8str );
}
/* }}} */

/* {{{ Test if a string is in a given normalization form. */
PHP_FUNCTION( normalizer_is_normalized )
{
	char*	 	input = NULL;
	/* form is optional, defaults to FORM_C */
	zend_long		form = NORMALIZER_DEFAULT;
	size_t		input_len = 0;

	UChar*	 	uinput = NULL;
	int		uinput_len = 0;
	UErrorCode	status = U_ZERO_ERROR;

	UBool		uret = false;

	intl_error_reset( NULL );

	/* Parse parameters. */
	if( zend_parse_method_parameters( ZEND_NUM_ARGS(), getThis(), "s|l",
				&input, &input_len, &form) == FAILURE )
	{
		RETURN_THROWS();
	}

	switch(form) {
		case NORMALIZER_FORM_D:
		case NORMALIZER_FORM_KD:
		case NORMALIZER_FORM_C:
		case NORMALIZER_FORM_KC:
		case NORMALIZER_FORM_KC_CF:
			break;
		default:
			zend_argument_value_error(2, "must be a a valid normalization form");
			RETURN_THROWS();
	}


	/*
	 * Test normalization of string (converting it to UTF-16 first).
	 */

	/* First convert the string to UTF-16. */
	intl_convert_utf8_to_utf16(&uinput, &uinput_len, input, input_len, &status );

	if( U_FAILURE( status ) )
	{
		/* Set global error code. */
		intl_error_set_code( NULL, status );

		/* Set error messages. */
		intl_error_set_custom_msg( NULL, "Error converting string to UTF-16.");
		if (uinput) {
			efree( uinput );
		}
		RETURN_FALSE;
	}


	/* test string */
	uret = intl_is_normalized(form, uinput, uinput_len, &status);

	efree( uinput );

	/* Bail out if an unexpected error occurred. */
	if( U_FAILURE(status)  ) {
		/* Set error messages. */
		intl_error_set_custom_msg( NULL,"Error testing if string is the given normalization form.");
		RETURN_FALSE;
	}

	if ( uret )
		RETURN_TRUE;

	RETURN_FALSE;
}
/* }}} */

/* {{{ Returns the Decomposition_Mapping property for the given UTF-8 encoded code point. */
PHP_FUNCTION( normalizer_get_raw_decomposition )
{
	char* input = NULL;
	size_t input_length = 0;

	UChar32 codepoint = -1;
	int32_t offset = 0;

	UErrorCode status = U_ZERO_ERROR;
	const UNormalizer2 *norm;
	UChar decomposition[32];
	int32_t decomposition_length;

	zend_long form = NORMALIZER_DEFAULT;

	intl_error_reset(NULL);

	ZEND_PARSE_PARAMETERS_START(1, 2)
		Z_PARAM_STRING(input, input_length)
		Z_PARAM_OPTIONAL
		Z_PARAM_LONG(form)
	ZEND_PARSE_PARAMETERS_END();

	norm = intl_get_normalizer(form, &status);

	U8_NEXT(input, offset, input_length, codepoint);
	if ((size_t)offset != input_length) {
		intl_error_set_code(NULL, U_ILLEGAL_ARGUMENT_ERROR);
		intl_error_set_custom_msg(NULL, "Input string must be exactly one UTF-8 encoded code point long.");
		return;
	}

	if ((codepoint < UCHAR_MIN_VALUE) || (codepoint > UCHAR_MAX_VALUE)) {
		intl_error_set_code(NULL, U_ILLEGAL_ARGUMENT_ERROR);
		intl_error_set_custom_msg(NULL, "Code point out of range");
		return;
	}

	decomposition_length = unorm2_getRawDecomposition(norm, codepoint, decomposition, 32, &status);
	if (decomposition_length == -1) {
		RETURN_NULL();
	}

	RETVAL_NEW_STR(intl_convert_utf16_to_utf8(decomposition, decomposition_length, &status));
}
/* }}} */
