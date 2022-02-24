struct SillyStruct : Sequence<Int> {}
// expected-error@-1 {{cannot inherit from protocol type with generic argument 'Sequence<Int>'}}
// expected-error@-2 {{type 'SillyStruct' does not conform to protocol 'Sequence'}}
