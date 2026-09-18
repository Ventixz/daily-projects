<?php

namespace App\Http\Requests;

use Illuminate\Contracts\Validation\ValidationRule;
use Illuminate\Foundation\Http\FormRequest;
use Illuminate\Validation\Rule;

class StoreBookRequest extends FormRequest
{
    /**
     * Determine if the user is authorized to make this request.
     */
    public function authorize(): bool
    {
        return true;
    }

    /**
     * Get the validation rules that apply to the request.
     *
     * @return array<string, ValidationRule|array<mixed>|string>
     */
    public function rules(): array
    {
        return [
            'title' => ['required', 'string', 'max:255'],
            'author' => ['required', 'string', 'max:255'],
            'isbn' => ['required', 'string', 'max:32', Rule::unique('books', 'isbn')],
            'status' => ['required', Rule::in(['available', 'checked_out'])],
            // A due date only makes sense while the book is checked out, and
            // it stops making sense the moment it's returned - so the two
            // rules below enforce both directions, not just "present when
            // checked out".
            'due_date' => [
                'nullable',
                'required_if:status,checked_out',
                'prohibited_unless:status,checked_out',
                'date',
                'after_or_equal:today',
            ],
        ];
    }
}
