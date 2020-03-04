final public class Subclass: Base {}

public protocol PublicProtocol: BaseProtocol {}

// expected-no-dependency {{main.BaseProtocol}}
// expected-provides {{Subclass}}
// expected-provides {{Base}}
// expected-provides {{PublicProtocol}}
// expected-provides {{BaseProtocol}}
// expected-cascading-member {{main.Base.init}}
// expected-cascading-superclass {{main.Base}}
// expected-cascading-member {{main.Subclass.init}}
// expected-cascading-member {{main.Subclass.deinit}}
